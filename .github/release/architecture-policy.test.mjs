import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import test from 'node:test';
import yaml from 'js-yaml';

const workflow = yaml.load(readFileSync(new URL('../workflows/receiver.yml', import.meta.url), 'utf8'));

test('CI retains exactly three build configurations on AMD64 only', () => {
  const matrix = workflow.jobs.reference.strategy.matrix;
  assert.deepEqual(matrix.platform, [{architecture: 'amd64', runner: 'ubuntu-latest'}]);
  assert.deepEqual(matrix.build, ['Release', 'ASan-UBSan', 'TSan']);
});

test('publication downloads only the tested AMD64 package', () => {
  const downloads = workflow.jobs.release.steps.filter(step => step.uses === 'actions/download-artifact@v4');
  assert.deepEqual(downloads.map(step => step.with.name), ['deb-ubuntu-26.04-amd64-${{ github.sha }}']);
});
