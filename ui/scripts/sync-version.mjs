#!/usr/bin/env node
import { readFileSync, writeFileSync, existsSync } from 'node:fs';
import { join, dirname } from 'node:path';

const rootArgIdx = process.argv.indexOf('--root');
const root = rootArgIdx >= 0 ? process.argv[rootArgIdx + 1] : join(process.cwd(), 'src-tauri');

const tauriConf = JSON.parse(readFileSync(join(root, 'tauri.conf.json'), 'utf-8'));
const version = tauriConf.version;

if (!version) {
  console.error('No version in tauri.conf.json');
  process.exit(1);
}

// In test mode, root is a temp dir with just tauri.conf.json + package.json.
// In production, root is ui/src-tauri and sibling dirs hold the other files.

// package.json (sibling of root in production, or in root dir in test)
const pkgPath = existsSync(join(root, '..', 'package.json'))
  ? join(root, '..', 'package.json')
  : join(root, 'package.json');
if (existsSync(pkgPath)) {
  const pkg = JSON.parse(readFileSync(pkgPath, 'utf-8'));
  pkg.version = version;
  writeFileSync(pkgPath, JSON.stringify(pkg, null, 2) + '\n');
}

// Cargo.toml (in root)
const cargoPath = join(root, 'Cargo.toml');
if (existsSync(cargoPath)) {
  let cargo = readFileSync(cargoPath, 'utf-8');
  cargo = cargo.replace(/version\s*=\s*"[^"]*"/, `version = "${version}"`);
  writeFileSync(cargoPath, cargo);
}

// CMakeLists.txt (../../native/ from root in production)
const cmakePath = join(root, '..', '..', 'native', 'CMakeLists.txt');
if (existsSync(cmakePath)) {
  let cmake = readFileSync(cmakePath, 'utf-8');
  cmake = cmake.replace(/(project\([^)]*VERSION\s+)[\d.]+/, `$1${version}`);
  writeFileSync(cmakePath, cmake);
}

// vcpkg.json (../../native/ from root in production)
const vcpkgPath = join(root, '..', '..', 'native', 'vcpkg.json');
if (existsSync(vcpkgPath)) {
  const vcpkg = JSON.parse(readFileSync(vcpkgPath, 'utf-8'));
  vcpkg['version-string'] = version;
  writeFileSync(vcpkgPath, JSON.stringify(vcpkg, null, 2) + '\n');
}

console.log(`Synced version ${version} to all files`);
