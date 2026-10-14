import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import test from 'node:test';
import yaml from 'js-yaml';

const workflow = yaml.load(readFileSync(new URL('../workflows/receiver.yml', import.meta.url), 'utf8'));
const build = workflow.jobs.reference;
const toolchain = yaml.load(readFileSync(new URL('../actions/setup-toolchain/action.yml', import.meta.url), 'utf8'));

test('workflow actions use Node.js 24 releases, including the shared compiler cache', () => {
  const versions = new Map([
    ['actions/checkout', 'v6'],
    ['actions/setup-node', 'v6'],
    ['actions/cache', 'v5'],
    ['actions/upload-artifact', 'v6'],
    ['actions/download-artifact', 'v7']
  ]);
  const steps = [...Object.values(workflow.jobs).flatMap(job => job.steps), ...toolchain.runs.steps];
  for (const step of steps.filter(step => step.uses?.startsWith('actions/'))) {
    const [action, version] = step.uses.split('@');
    assert.equal(version, versions.get(action), step.uses);
  }
});

test('runner hosts stay on Ubuntu 24.04 while the build container stays on Ubuntu 26.04', () => {
  for (const job of Object.values(workflow.jobs)) {
    if (job.strategy?.matrix.platform) {
      for (const platform of job.strategy.matrix.platform) assert.equal(platform.runner, 'ubuntu-24.04');
    } else {
      assert.equal(job['runs-on'], 'ubuntu-24.04');
    }
  }
  assert.equal(build.container, 'ubuntu:26.04');
});

test('workflow checks advertise their purpose without changing job dependencies', () => {
  assert.equal(workflow.name, 'Build, validate and release');
  assert.equal(workflow.jobs.plan.name, 'Plan release version');
  assert.equal(build.name, '${{ matrix.purpose }} / AMD64');
  assert.deepEqual(build.strategy.matrix.include, [
    {build: 'Release', purpose: 'Verify release package'},
    {build: 'ASan-UBSan', purpose: 'Check memory safety and undefined behavior'},
    {build: 'TSan', purpose: 'Check thread safety'}
  ]);
  assert.equal(workflow.jobs['release-policy'].name, 'Validate release automation');
  assert.equal(workflow.jobs.release.name, 'Publish release');
  assert.equal(build.needs, 'plan');
  assert.deepEqual(workflow.jobs.release.needs, ['plan', 'reference', 'release-policy']);
});

test('each Meson configuration uses the pinned compilers and instruments both languages', () => {
  const configure = build.steps.find(step => step.name === 'Configure');
  assert.equal(configure.env.BUILD, '${{ matrix.build }}');
  for (const [configuration, type, sanitizer, flags] of [
    ['Release', 'release', 'none', ''],
    ['ASan-UBSan', 'debug', 'address,undefined', '-fno-sanitize-recover=all -fno-omit-frame-pointer'],
    ['TSan', 'debug', 'thread', '-fno-omit-frame-pointer']
  ]) {
    const args = execFileSync('bash', ['-eu', '-c',
      'asdf() { printf "/toolchain/%s\\n" "$2"; }\n' +
      'meson() { printf "%s\\n" "CC=$CC" "CXX=$CXX" "$@"; }\n' + configure.run],
    {encoding: 'utf8', env: {...process.env, BUILD: configuration}}).trim().split('\n');
    assert.ok(args.includes('CC=/toolchain/clang'));
    assert.ok(args.includes('CXX=/toolchain/clang++'));
    assert.ok(args.includes(`--buildtype=${type}`));
    assert.ok(args.includes(`-Db_sanitize=${sanitizer}`));
    for (const language of ['c', 'cpp']) assert.ok(args.includes(`-D${language}_args=${flags}`));
  }
  assert.equal(build.strategy['fail-fast'], false);
  assert.equal(build.steps.find(step => step.name === 'Test').run,
    'meson test -C build/meson --print-errorlogs --num-processes 1');
  assert.equal(build.steps.find(step => step.name === 'Build').run,
    'meson compile -C build/meson -j 2');
});

test('installation and packaging run only in Release while instrumentation runs only in sanitizers', () => {
  for (const name of ['Verify staged install', 'Build Debian package', 'Upload Debian package']) {
    assert.equal(build.steps.find(step => step.name === name).if, "matrix.build == 'Release'");
  }
  assert.equal(build.steps.find(step => step.name === 'Verify sanitizer instrumentation').if,
    "matrix.build != 'Release'");
  const upload = build.steps.find(step => step.name === 'Upload Debian package');
  assert.equal(upload.with.name, 'deb-ubuntu-26.04-${{ matrix.platform.architecture }}-${{ github.sha }}');
  assert.equal(upload.with['if-no-files-found'], 'error');
  const diagnostics = build.steps.find(step => step.name === 'Upload failed test diagnostics');
  assert.equal(diagnostics.if, 'failure()');
  assert.equal(diagnostics.with.path, 'build/meson/meson-logs/');
});

test('Release stages and checks the native Meson Debian package', () => {
  const installation = build.steps.find(step => step.name === 'Verify staged install').run;
  assert.match(installation, /meson install -C build\/meson/);
  assert.match(installation, /ruby tests\/install_contract_test.rb/);
  const packaging = build.steps.find(step => step.name === 'Build Debian package').run;
  assert.match(packaging, /meson compile -C build\/meson deb/);
  assert.match(packaging, /ruby tests\/install_contract_test.rb/);
  assert.match(packaging, /sha256sum --check/);
  const dependencies = build.steps.find(step => step.name === 'Install C++26 build dependencies').run;
  assert.match(dependencies, /\bmeson\b/);
  assert.match(dependencies, /\bruby\b/);
  assert.doesNotMatch(dependencies, /\bcmake\b/);
});

test('PRs validate without publication and publication requires successful validation', () => {
  assert.equal(workflow.jobs.plan.if, "github.event_name == 'push'");
  assert.equal(build.if, "always() && (needs.plan.result == 'success' || needs.plan.result == 'skipped')");
  const validation = workflow.jobs['release-policy'];
  assert.equal(validation.if, undefined);
  assert.ok(validation.steps.some(step => step.run === 'npm test --prefix .github/release'));
  const publication = workflow.jobs.release;
  // Without a status function, GitHub applies success() to all required jobs.
  assert.equal(publication.if, "github.event_name == 'push'");
  assert.deepEqual(publication.needs, ['plan', 'reference', 'release-policy']);
  assert.equal(publication.concurrency['cancel-in-progress'], false);
  const freshness = publication.steps.find(step => step.id === 'freshness');
  assert.match(freshness.run, /git rev-parse FETCH_HEAD/);
  for (const step of publication.steps.filter(step => step.name !== freshness.name &&
    (step.uses?.startsWith('actions/download-artifact@') || step.uses?.startsWith('actions/setup-node@') || step.run))) {
    if (step.if !== 'failure()') assert.equal(step.if, "steps.freshness.outputs.current == 'true'");
  }
  assert.ok(publication.steps.every(step => !/meson (?:setup|compile)|dpkg-deb --build/.test(step.run ?? '')));
});

test('all workflow and compiler setup steps have descriptive display names', () => {
  const steps = [...Object.values(workflow.jobs).flatMap(job => job.steps), ...toolchain.runs.steps];
  for (const step of steps) assert.ok(step.name?.trim(), `Unnamed step: ${step.uses ?? step.run}`);
});
