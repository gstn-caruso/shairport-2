import assert from 'node:assert/strict';
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
    {build: 'Debug', purpose: 'Check debug build'},
    {build: 'ASan-UBSan', purpose: 'Check memory safety and undefined behavior'},
    {build: 'TSan', purpose: 'Check thread safety'}
  ]);
  assert.equal(workflow.jobs['release-policy'].name, 'Validate release automation');
  assert.equal(workflow.jobs.release.name, 'Publish release');
  assert.equal(build.needs, 'plan');
  assert.deepEqual(workflow.jobs.release.needs, ['plan', 'reference', 'release-policy']);
});

test('all workflow and compiler setup steps have descriptive display names', () => {
  const steps = [...Object.values(workflow.jobs).flatMap(job => job.steps), ...toolchain.runs.steps];
  for (const step of steps) assert.ok(step.name?.trim(), `Unnamed step: ${step.uses ?? step.run}`);
});
