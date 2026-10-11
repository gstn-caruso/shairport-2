import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {Writable} from 'node:stream';
import {pathToFileURL} from 'node:url';
import test from 'node:test';
import {prepare, verifyRelease} from '@semantic-release/exec';
import config from './release.config.mjs';

const version = '5.7.0';
const fixture = () => {
  const cwd = mkdtempSync(join(tmpdir(), 'release-packages-'));
  mkdirSync(join(cwd, 'build/release/packages'), {recursive: true});
  return cwd;
};
const packageFor = (cwd, architecture, packageVersion = version, filenameArchitecture = architecture) => {
  const root = join(cwd, `fixture-${filenameArchitecture}`);
  mkdirSync(join(root, 'DEBIAN'), {recursive: true});
  writeFileSync(join(root, 'DEBIAN/control'), `Package: shairport-2\nVersion: ${packageVersion}\nArchitecture: ${architecture}\nMaintainer: Test <test@example.com>\nDescription: Release fixture\n`);
  execFileSync('dpkg-deb', ['--build', root, join(cwd, 'build/release/packages', `shairport-2_${version}_${filenameArchitecture}.deb`)], {stdio: 'pipe'});
};
const silentOutput = () => new Writable({write(chunk, encoding, callback) { callback(); }});
const context = cwd => ({cwd, env: {...process.env, RELEASE_VERSION: version}, nextRelease: {version}, logger: {log() {}}, stdout: silentOutput(), stderr: silentOutput()});
const execution = config.plugins.find(([name]) => name === '@semantic-release/exec')[1];

test('publication prepares the same version from the AMD64 package alone', async () => {
  const cwd = fixture();
  try {
    packageFor(cwd, 'amd64');
    await prepare(execution, context(cwd));
    assert.equal(readFileSync(join(cwd, 'VERSION'), 'utf8'), `${version}\n`);
    const checksums = readFileSync(join(cwd, 'build/release/packages/SHA256SUMS'), 'utf8');
    assert.equal(checksums.trim().split('\n').length, 1);
    execFileSync('sha256sum', ['--check', 'SHA256SUMS'], {cwd: join(cwd, 'build/release/packages'), stdio: 'pipe'});
  } finally {
    rmSync(cwd, {recursive: true, force: true});
  }
});

for (const [failure, arrange] of [
  ['missing AMD64', () => {}],
  ['wrong version', cwd => packageFor(cwd, 'amd64', '5.6.0')],
  ['wrong architecture', cwd => packageFor(cwd, 'arm64', version, 'amd64')],
  ['unexpected extra package', cwd => {
    packageFor(cwd, 'amd64');
    packageFor(cwd, 'arm64');
  }],
  ['changed plan', (cwd, invocation) => {
    packageFor(cwd, 'amd64');
    invocation.env.RELEASE_VERSION = '5.8.0';
  }]
]) {
  test(`publication rejects ${failure} before updating VERSION`, async () => {
    const cwd = fixture();
    try {
      const invocation = context(cwd);
      arrange(cwd, invocation);
      await assert.rejects(prepare(execution, invocation));
      assert.equal(existsSync(join(cwd, 'VERSION')), false);
    } finally {
      rmSync(cwd, {recursive: true, force: true});
    }
  });
}

test('the dry-run verification hook emits the semantic-release version without preparing a release', async () => {
  const {default: plan} = await import('./plan.config.mjs');
  const cwd = fixture();
  try {
    const output = join(cwd, 'output');
    const invocation = context(cwd);
    invocation.env.GITHUB_OUTPUT = output;
    invocation.options = {dryRun: true};
    const plugin = plan.plugins.find(([name]) => name === '@semantic-release/exec')[1];
    await verifyRelease(plugin, invocation);
    assert.equal(readFileSync(output, 'utf8'), `version=${version}\n`);
    assert.equal(existsSync(join(cwd, 'VERSION')), false);
  } finally {
    rmSync(cwd, {recursive: true, force: true});
  }
});

for (const [message, plannedVersion] of [
  ['feat: provide ARM64 packages', '5.7.0'],
  ['docs: explain receiver setup', null]
]) {
test(`semantic-release dry-run plans ${plannedVersion ?? 'no release'} for ${message}`, async () => {
  const directory = fixture();
  try {
    const remote = join(directory, 'remote.git');
    const cwd = join(directory, 'repository');
    execFileSync('git', ['init', '--bare', '--initial-branch=master', remote], {stdio: 'pipe'});
    execFileSync('git', ['clone', remote, cwd], {stdio: 'pipe'});
    const git = (...args) => execFileSync('git', args, {cwd, stdio: 'pipe'});
    git('checkout', '-b', 'master');
    git('config', 'user.email', 'test@example.com');
    git('config', 'user.name', 'Release Test');
    git('commit', '--allow-empty', '-m', 'chore: baseline');
    git('tag', 'v5.6.0');
    git('commit', '--allow-empty', '-m', message);
    git('push', 'origin', 'master', '--tags');
    const output = join(directory, 'output');
    const integration = `
      import semanticRelease from ${JSON.stringify(import.meta.resolve('semantic-release'))};
      import plan from ${JSON.stringify(new URL('./plan.config.mjs', import.meta.url).href)};
      const result = await semanticRelease({
        ...plan, repositoryUrl: ${JSON.stringify(pathToFileURL(remote).href)}, dryRun: true, ci: false,
        plugins: plan.plugins.filter(([name]) => ['@semantic-release/commit-analyzer', '@semantic-release/release-notes-generator', '@semantic-release/exec'].includes(name))
      }, {env: {PATH: process.env.PATH, GITHUB_OUTPUT: process.env.GITHUB_OUTPUT}});
      if (${plannedVersion === null ? 'result !== false' : `result.nextRelease.version !== '${plannedVersion}'`}) process.exit(1);
    `;
    execFileSync(process.execPath, ['--input-type=module', '-e', integration], {
      cwd,
      env: {...process.env, GITHUB_OUTPUT: output, GITHUB_ACTIONS: 'true', GITHUB_EVENT_NAME: 'pull_request', GITHUB_REF: 'refs/pull/25/merge', GITHUB_HEAD_REF: 'feat/arm64-debian-releases'},
      stdio: 'pipe'
    });
    if (plannedVersion === null) {
      assert.equal(existsSync(output), false);
    } else {
      assert.equal(readFileSync(output, 'utf8'), `version=${plannedVersion}\n`);
    }
    assert.equal(existsSync(join(cwd, 'VERSION')), false);
    assert.throws(() => git('rev-parse', '--verify', 'refs/tags/v5.7.0'));
  } finally {
    rmSync(directory, {recursive: true, force: true});
  }
});
}
