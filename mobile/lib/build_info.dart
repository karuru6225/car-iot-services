// ビルド時に --dart-define=GIT_HASH=... で埋め込まれる（未指定時は 'unknown'）。
// mobile/run.ps1 経由で flutter run すると自動的に現在の HEAD hash が渡される。
const String kGitHash = String.fromEnvironment('GIT_HASH', defaultValue: 'unknown');
