import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import test from 'node:test';
import yaml from 'js-yaml';

const workflow = yaml.load(readFileSync(new URL('../workflows/receiver.yml', import.meta.url), 'utf8'));
const build = workflow.jobs.reference;
const toolchain = yaml.load(readFileSync(new URL('../actions/setup-toolchain/action.yml', import.meta.url), 'utf8'));

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

test('each configuration uses the expected build type and sanitizer flags in both languages', () => {
  const configure = build.steps.find(step => step.name === 'Configure');
  assert.equal(configure.env.BUILD, '${{ matrix.build }}');
  for (const [configuration, type, flags] of [
    ['Release', 'Release', ''],
    ['ASan-UBSan', 'Debug', '-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'],
    ['TSan', 'Debug', '-fsanitize=thread -fno-omit-frame-pointer']
  ]) {
    const args = execFileSync('bash', ['-eu', '-c',
      'cmake() { printf "%s\\n" "$@"; }\n' + configure.run],
    {encoding: 'utf8', env: {...process.env, BUILD: configuration}}).trim().split('\n');
    assert.ok(args.includes(`-DCMAKE_BUILD_TYPE=${type}`));
    for (const language of ['C', 'CXX']) assert.ok(args.includes(`-DCMAKE_${language}_FLAGS=${flags}`));
  }
  assert.equal(build.strategy['fail-fast'], false);
  assert.equal(build.steps.find(step => step.name === 'Test').run,
    'ctest --test-dir build/cmake --no-tests=error --output-on-failure');
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
  assert.equal(diagnostics.with.path, 'build/cmake/Testing/Temporary/');
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
    (step.uses === 'actions/download-artifact@v4' || step.uses === 'actions/setup-node@v4' || step.run))) {
    if (step.if !== 'failure()') assert.equal(step.if, "steps.freshness.outputs.current == 'true'");
  }
  assert.ok(publication.steps.every(step => !/cmake|cpack/.test(step.run ?? '')));
});

test('all workflow and compiler setup steps have descriptive display names', () => {
  const steps = [...Object.values(workflow.jobs).flatMap(job => job.steps), ...toolchain.runs.steps];
  for (const step of steps) assert.ok(step.name?.trim(), `Unnamed step: ${step.uses ?? step.run}`);
});
