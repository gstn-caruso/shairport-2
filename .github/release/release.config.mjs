export default {
  branches: ['master', 'main'],
  tagFormat: 'v${version}',
  plugins: [
    ['@semantic-release/commit-analyzer', {
      preset: 'conventionalcommits',
      releaseRules: [
        {breaking: true, release: 'major'},
        {type: 'feat', release: 'minor'},
        {type: 'fix', release: 'patch'},
        {type: 'perf', release: 'patch'},
        {revert: true, release: false}
      ]
    }],
    ['@semantic-release/release-notes-generator', {
      preset: 'conventionalcommits',
      presetConfig: {
        types: [
          {type: 'feat', section: 'Features'},
          {type: 'feature', section: 'Features'},
          {type: 'fix', section: 'Bug Fixes'},
          {type: 'perf', section: 'Performance Improvements'},
          {type: 'revert', section: 'Reverts'},
          {type: 'docs', section: 'Documentation'},
          {type: 'style', section: 'Styles'},
          {type: 'chore', section: 'Miscellaneous Chores'},
          {type: 'refactor', section: 'Code Refactoring'},
          {type: 'test', section: 'Tests'},
          {type: 'build', section: 'Build System'},
          {type: 'ci', section: 'Continuous Integration'}
        ]
      }
    }],
    ['@semantic-release/changelog', {changelogFile: 'CHANGELOG.md'}],
    ['@semantic-release/exec', {
      prepareCmd: `set -eu
test "$RELEASE_VERSION" = '<%= nextRelease.version %>'
set -- build/release/packages/*.deb
test "$#" -eq 1
package="build/release/packages/shairport-2_<%= nextRelease.version %>_amd64.deb"
test -f "$package"
test "$(dpkg-deb -f "$package" Package)" = shairport-2
test "$(dpkg-deb -f "$package" Version)" = '<%= nextRelease.version %>'
test "$(dpkg-deb -f "$package" Architecture)" = amd64
(cd build/release/packages && sha256sum *.deb > SHA256SUMS)
printf '%s\\n' '<%= nextRelease.version %>' > VERSION`
    }],
    ['@semantic-release/git', {
      assets: ['VERSION', 'CHANGELOG.md'],
      message: 'chore(release): ${nextRelease.version} [skip ci]\n\n${nextRelease.notes}'
    }],
    ['@semantic-release/github', {
      assets: ['build/release/packages/*.deb', 'build/release/packages/SHA256SUMS'],
      successComment: false,
      failComment: false,
      failTitle: false,
      releasedLabels: false
    }]
  ]
};
