// Read-only staged Linux package audit. Never installs, prunes or launches files.
import { lstatSync, statSync, readdirSync, realpathSync, readFileSync } from 'node:fs';
import { resolve, relative, sep } from 'node:path';

const input = process.argv[2];
if (!input) {
    console.error('Usage: node qa/check_release_layout.mjs <staged-linux-package>');
    process.exit(2);
}
const root = realpathSync(input);
const problems = [];
const files = [];
const sizes = new Map();
const inodes = new Set();
const isInside = path => {
    const rel = relative(root, path);
    return rel !== '..' && !rel.startsWith('..' + sep) && !rel.startsWith(sep);
};
function usableFile(path) {
    try {
        const resolved = realpathSync(resolve(root, path));
        const info = statSync(resolved);
        return isInside(resolved) && info.isFile() && info.size > 0;
    } catch { return false; }
}
function scan(directory) {
    for (const entry of readdirSync(directory, { withFileTypes: true })) {
        const path = resolve(directory, entry.name);
        const rel = relative(root, path).split(sep).join('/');
        const info = lstatSync(path);
        files.push(rel);
        if (info.isSymbolicLink()) {
            try {
                if (!isInside(realpathSync(path))) problems.push(`External symlink: ${rel}`);
            } catch { problems.push(`Broken symlink: ${rel}`); }
            continue;
        }
        if (info.isDirectory()) { scan(path); continue; }
        if (!info.isFile()) { problems.push(`Unexpected special file: ${rel}`); continue; }
        const key = `${info.dev}:${info.ino}`;
        if (inodes.has(key)) continue;
        inodes.add(key);
        const section = rel.split('/')[0];
        sizes.set(section, (sizes.get(section) || 0) + info.size);
    }
}
scan(root);
for (const required of ['bin/gerritpilot', 'bin/qt.conf', 'qml/QtQuick/Controls/Basic/qmldir',
    'qml/QtQuick/Dialogs/qmldir', 'plugins/platforms/libqxcb.so',
    'share/licenses/GerritPilot/LICENSE']) {
    if (!usableFile(required))
        problems.push(`Missing runtime file: ${required}`);
}
if (usableFile('bin/gerritpilot') && !(statSync(resolve(root, 'bin/gerritpilot')).mode & 0o111))
    problems.push('Application entry is not executable');
for (const library of ['Core', 'Gui', 'Qml', 'Quick', 'Network'])
    if (!files.some(path => new RegExp(`^lib/libQt6${library}\\.so(?:\\.[0-9]+)*$`).test(path) && usableFile(path)))
        problems.push(`Missing runtime library: Qt6${library}`);
if (!files.some(path => /^plugins\/tls\/[^/]+\.so$/.test(path) && usableFile(path)))
    problems.push('Missing TLS backend');
for (const path of files) {
    if (/^qml\/QtTest(?:\/|$)|^plugins\/qmltooling(?:\/|$)|^lib\/libQt6(?:QuickTest|Test)\.so/.test(path))
        problems.push(`Development dependency: ${path}`);
    if (/^qml\/QtQuick\/Controls\/(?:Fusion|Material|Imagine|Universal|FluentWinUI3|Windows|macOS|iOS)(?:\/|$)/.test(path)
        || /^lib\/libQt6QuickControls2(?:Fusion|Material|Imagine|Universal|FluentWinUI3|Windows|MacOS|IOS)/.test(path))
        problems.push(`Unused Controls style: ${path}`);
    if (/^translations\/.+\.qm$/.test(path)) problems.push(`Unused Qt translation: ${path}`);
    if (/(?:^|\/)(?:\.git|\.repo|\.ssh|qa|CMakeFiles)(?:\/|$)|(?:^|\/)CMakeCache\.txt$/.test(path))
        problems.push(`Source, settings or build tree included: ${path}`);
}
for (const [section, size] of sizes)
    console.log(`${section}: ${(size / 1048576).toFixed(2)} MiB`);
console.log(`Total regular-file bytes: ${([...sizes.values()].reduce((a, b) => a + b, 0) / 1048576).toFixed(2)} MiB`);
// Report categories once; this is not a license, ELF dependency or runtime audit.
const categories = [...new Set(problems.map(problem => problem.split(':')[0]))];
for (const category of categories) {
    const matches = problems.filter(problem => problem.split(':')[0] === category);
    console.error(`${category}: ${matches.length} item(s); ${matches[0]}`);
}
console.log(problems.length ? 'FAIL: package is not ready for minimal deployment acceptance' : 'PASS: layout only; runtime and license acceptance still required');
process.exitCode = problems.length ? 1 : 0;
