/**
 * Static verification of a built KoreDB Node.js addon (`koredbjs.node`).
 *
 * The addon is compiled against Node-API, whose ABI is stable across Node.js
 * and Electron, so one binary per platform is supposed to load in both.
 * "Supposed to" is doing a lot of work there: the property is easy to lose in
 * the link step and impossible to notice from a passing Node.js test suite,
 * because every way of losing it still works under plain `node`. This checker
 * reads the produced binary and asserts the invariants that keep the promise
 * true, per object format:
 *
 *   PE (Windows)   - node.exe has to be a DELAY import, never a normal one, so
 *                    that cmake-js's win_delay_load_hook can bind the napi_*
 *                    symbols to whatever executable is hosting the addon. It
 *                    must also not import the VC++ runtime, which is absent on
 *                    a machine without the redistributable.
 *   ELF (Linux)    - nothing node-shaped in DT_NEEDED (the napi_* symbols are
 *                    undefined and resolved from the host at dlopen time), and
 *                    the glibc / libstdc++ symbol versions it demands stay at
 *                    or below the floor the manylinux build promises.
 *   Mach-O (macOS) - nothing node-shaped in LC_LOAD_DYLIB (same flat-namespace
 *                    lookup as ELF) and a deployment target no newer than the
 *                    oldest macOS the supported Electron releases still run on.
 *
 * Usage:
 *   node scripts/dist/verify-addon.js <addon> [more addons...] [options]
 *
 *   --max-glibc=<v>     highest allowed GLIBC_ symbol version    (default 2.28)
 *   --max-glibcxx=<v>   highest allowed GLIBCXX_ symbol version  (default 3.4.25)
 *   --max-macos=<v>     highest allowed deployment target        (default 11.0)
 *   --allow-dynamic-crt downgrade a Windows VC++ runtime import to a warning
 *
 * Exits non-zero when any check fails.
 */

import { readFileSync } from "node:fs";

const NAPI_ENTRY = "napi_register_module_v1";

type Level = "pass" | "warn" | "fail";

interface Finding {
  level: Level;
  text: string;
}

interface Options {
  maxGlibc: string;
  maxGlibcxx: string;
  maxMacos: string;
  allowDynamicCrt: boolean;
}

/** Compare dotted version strings numerically, so that "2.9" sorts below "2.28". */
function compareVersions(a: string, b: string): number {
  const left = a.split(".").map((part) => Number.parseInt(part, 10) || 0);
  const right = b.split(".").map((part) => Number.parseInt(part, 10) || 0);
  for (let i = 0; i < Math.max(left.length, right.length); i++) {
    const difference = (left[i] ?? 0) - (right[i] ?? 0);
    if (difference !== 0) {
      return difference < 0 ? -1 : 1;
    }
  }
  return 0;
}

function readCString(buf: Buffer, offset: number): string {
  let end = offset;
  while (end < buf.length && buf[end] !== 0) {
    end++;
  }
  return buf.toString("utf8", offset, end);
}

/** A dependency named after a JS runtime is one Electron will not provide. */
function isHostRuntime(name: string): boolean {
  return /(^|\/)(lib)?node([.-]|$)/i.test(name);
}

// --- PE (Windows) ------------------------------------------------------------

interface PeSection {
  virtualAddress: number;
  virtualSize: number;
  rawOffset: number;
  rawSize: number;
}

const PE_MACHINES: Record<number, string> = {
  0x014c: "x86",
  0x01c0: "arm",
  0x8664: "x64",
  0xaa64: "arm64",
};

function verifyPe(buf: Buffer, options: Options): Finding[] {
  const findings: Finding[] = [];
  const peOffset = buf.readUInt32LE(0x3c);
  if (buf.toString("ascii", peOffset, peOffset + 4) !== "PE\0\0") {
    return [{ level: "fail", text: "not a PE image (missing PE signature)" }];
  }

  const coff = peOffset + 4;
  const machine = buf.readUInt16LE(coff);
  const sectionCount = buf.readUInt16LE(coff + 2);
  const optionalHeaderSize = buf.readUInt16LE(coff + 16);
  const optional = coff + 20;
  const pe32Plus = buf.readUInt16LE(optional) === 0x20b;
  const dataDirectories = optional + (pe32Plus ? 112 : 96);

  const sections: PeSection[] = [];
  const sectionTable = optional + optionalHeaderSize;
  for (let i = 0; i < sectionCount; i++) {
    const entry = sectionTable + i * 40;
    sections.push({
      virtualSize: buf.readUInt32LE(entry + 8),
      virtualAddress: buf.readUInt32LE(entry + 12),
      rawSize: buf.readUInt32LE(entry + 16),
      rawOffset: buf.readUInt32LE(entry + 20),
    });
  }

  const toOffset = (rva: number): number | null => {
    for (const section of sections) {
      const span = Math.max(section.virtualSize, section.rawSize);
      if (rva >= section.virtualAddress && rva < section.virtualAddress + span) {
        return section.rawOffset + (rva - section.virtualAddress);
      }
    }
    return null;
  };
  const stringAt = (rva: number): string => {
    const offset = toOffset(rva);
    return offset === null ? "<unreadable>" : readCString(buf, offset);
  };
  const directoryRva = (index: number): number => buf.readUInt32LE(dataDirectories + index * 8);

  const machineName = PE_MACHINES[machine] ?? `0x${machine.toString(16)}`;
  console.log(`  format: PE${pe32Plus ? "32+" : "32"}, machine ${machineName}`);

  // Normal imports: 20-byte descriptors with the DLL name RVA at +12,
  // terminated by an all-zero entry.
  const normalImports: string[] = [];
  const importRva = directoryRva(1);
  const importOffset = importRva === 0 ? null : toOffset(importRva);
  if (importOffset !== null) {
    for (let cursor = importOffset; ; cursor += 20) {
      const nameRva = buf.readUInt32LE(cursor + 12);
      const firstThunk = buf.readUInt32LE(cursor + 16);
      if (nameRva === 0 && firstThunk === 0) {
        break;
      }
      normalImports.push(stringAt(nameRva));
    }
  }

  // Delay imports: 32-byte descriptors with the DLL name RVA at +4.
  const delayImports: string[] = [];
  const delayRva = directoryRva(13);
  const delayOffset = delayRva === 0 ? null : toOffset(delayRva);
  if (delayOffset !== null) {
    for (let cursor = delayOffset; ; cursor += 32) {
      const nameRva = buf.readUInt32LE(cursor + 4);
      if (nameRva === 0) {
        break;
      }
      delayImports.push(stringAt(nameRva));
    }
  }

  console.log(`  normal imports: ${normalImports.join(", ") || "(none)"}`);
  console.log(`  delay imports:  ${delayImports.join(", ") || "(none)"}`);

  let exportsNapiEntry = false;
  const exportRva = directoryRva(0);
  const exportOffset = exportRva === 0 ? null : toOffset(exportRva);
  if (exportOffset !== null) {
    const nameCount = buf.readUInt32LE(exportOffset + 24);
    const nameTable = toOffset(buf.readUInt32LE(exportOffset + 32));
    if (nameTable !== null) {
      for (let i = 0; i < nameCount; i++) {
        if (stringAt(buf.readUInt32LE(nameTable + i * 4)) === NAPI_ENTRY) {
          exportsNapiEntry = true;
          break;
        }
      }
    }
  }
  findings.push(
    exportsNapiEntry
      ? { level: "pass", text: `exports ${NAPI_ENTRY}` }
      : { level: "fail", text: `does not export ${NAPI_ENTRY}` }
  );

  const isHostExe = (name: string): boolean => /^(node|electron|nw)\.exe$/i.test(name);
  if (normalImports.some(isHostExe)) {
    findings.push({
      level: "fail",
      text:
        "the host executable is a NORMAL import, so the addon only loads under a " +
        "process of that exact name and dies inside electron.exe. Link the addon " +
        "with /DELAYLOAD:NODE.EXE and delayimp.",
    });
  } else if (delayImports.some(isHostExe)) {
    findings.push({
      level: "pass",
      text: "node.exe is a delay import, bound at first call to the running host (node.exe or electron.exe)",
    });
  } else {
    findings.push({
      level: "fail",
      text: "no node.exe import at all - the napi_* symbols have nothing to bind to",
    });
  }

  const redistributable = normalImports.filter((name) =>
    /^(vcruntime140|msvcp140|concrt140)/i.test(name)
  );
  if (redistributable.length === 0) {
    findings.push({ level: "pass", text: "static CRT - no VC++ redistributable required" });
  } else {
    findings.push({
      level: options.allowDynamicCrt ? "warn" : "fail",
      text:
        `imports the VC++ runtime (${redistributable.join(", ")}), which fails to load ` +
        "with Windows error 126 on a machine without the redistributable. Configure " +
        "with -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded.",
    });
  }

  return findings;
}

// --- ELF (Linux) -------------------------------------------------------------

interface ElfSection {
  name: string;
  type: number;
  offset: number;
  size: number;
  link: number;
  entrySize: number;
}

const ELF_MACHINES: Record<number, string> = {
  0x3e: "x86-64",
  0xb7: "aarch64",
};

const SHT_DYNAMIC = 6;
const SHT_DYNSYM = 11;
const SHT_GNU_VERNEED = 0x6ffffffe;
const DT_NULL = 0n;
const DT_NEEDED = 1n;

function verifyElf(buf: Buffer, options: Options): Finding[] {
  const findings: Finding[] = [];
  if (buf[4] !== 2) {
    return [{ level: "fail", text: "only 64-bit ELF is supported" }];
  }
  if (buf[5] !== 1) {
    return [{ level: "fail", text: "only little-endian ELF is supported" }];
  }

  const machine = buf.readUInt16LE(0x12);
  const sectionHeaderOffset = Number(buf.readBigUInt64LE(0x28));
  const sectionEntrySize = buf.readUInt16LE(0x3a);
  const sectionCount = buf.readUInt16LE(0x3c);
  const sectionNameIndex = buf.readUInt16LE(0x3e);

  const headers: Array<Omit<ElfSection, "name">> = [];
  const nameOffsets: number[] = [];
  for (let i = 0; i < sectionCount; i++) {
    const entry = sectionHeaderOffset + i * sectionEntrySize;
    nameOffsets.push(buf.readUInt32LE(entry));
    headers.push({
      type: buf.readUInt32LE(entry + 4),
      offset: Number(buf.readBigUInt64LE(entry + 24)),
      size: Number(buf.readBigUInt64LE(entry + 32)),
      link: buf.readUInt32LE(entry + 40),
      entrySize: Number(buf.readBigUInt64LE(entry + 56)),
    });
  }
  const sectionNames = headers[sectionNameIndex].offset;
  const sections: ElfSection[] = headers.map((header, index) => ({
    ...header,
    name: readCString(buf, sectionNames + nameOffsets[index]),
  }));

  const machineName = ELF_MACHINES[machine] ?? `0x${machine.toString(16)}`;
  console.log(`  format: ELF64, machine ${machineName}`);

  const needed: string[] = [];
  const dynamic = sections.find((section) => section.type === SHT_DYNAMIC);
  if (dynamic !== undefined) {
    const strings = sections[dynamic.link].offset;
    for (let cursor = dynamic.offset; cursor < dynamic.offset + dynamic.size; cursor += 16) {
      const tag = buf.readBigInt64LE(cursor);
      if (tag === DT_NULL) {
        break;
      }
      if (tag === DT_NEEDED) {
        needed.push(readCString(buf, strings + Number(buf.readBigUInt64LE(cursor + 8))));
      }
    }
  }
  console.log(`  DT_NEEDED: ${needed.join(", ") || "(none)"}`);

  // Symbol version requirements (GLIBC_2.28, GLIBCXX_3.4.25, ...) live in
  // .gnu.version_r: a Verneed list, each with a chain of Vernaux entries.
  const required = new Map<string, string>();
  const verneed = sections.find((section) => section.type === SHT_GNU_VERNEED);
  if (verneed !== undefined) {
    const strings = sections[verneed.link].offset;
    for (let entry = verneed.offset; ; ) {
      const auxCount = buf.readUInt16LE(entry + 2);
      const auxOffset = buf.readUInt32LE(entry + 8);
      const nextEntry = buf.readUInt32LE(entry + 12);
      let aux = entry + auxOffset;
      for (let i = 0; i < auxCount; i++) {
        const label = readCString(buf, strings + buf.readUInt32LE(aux + 8));
        const separator = label.lastIndexOf("_");
        if (separator > 0) {
          const family = label.slice(0, separator);
          const version = label.slice(separator + 1);
          const highest = required.get(family);
          if (highest === undefined || compareVersions(version, highest) > 0) {
            required.set(family, version);
          }
        }
        const nextAux = buf.readUInt32LE(aux + 12);
        if (nextAux === 0) {
          break;
        }
        aux += nextAux;
      }
      if (nextEntry === 0) {
        break;
      }
      entry += nextEntry;
    }
  }
  const versions = [...required.entries()].map(([family, version]) => `${family}_${version}`);
  console.log(`  highest symbol versions: ${versions.join(", ") || "(none)"}`);

  let exportsNapiEntry = false;
  const dynsym = sections.find((section) => section.type === SHT_DYNSYM);
  if (dynsym !== undefined && dynsym.entrySize > 0) {
    const strings = sections[dynsym.link].offset;
    const end = dynsym.offset + dynsym.size;
    for (let cursor = dynsym.offset; cursor < end; cursor += dynsym.entrySize) {
      if (readCString(buf, strings + buf.readUInt32LE(cursor)) === NAPI_ENTRY) {
        exportsNapiEntry = true;
        break;
      }
    }
  }
  findings.push(
    exportsNapiEntry
      ? { level: "pass", text: `exports ${NAPI_ENTRY}` }
      : { level: "fail", text: `does not export ${NAPI_ENTRY}` }
  );

  const hostLibraries = needed.filter(isHostRuntime);
  findings.push(
    hostLibraries.length === 0
      ? {
          level: "pass",
          text: "no link-time dependency on Node - napi_* symbols resolve from the host at dlopen",
        }
      : {
          level: "fail",
          text: `links against the host runtime (${hostLibraries.join(", ")}), which Electron does not provide`,
        }
  );

  const ceilings: Array<[string, string]> = [
    ["GLIBC", options.maxGlibc],
    ["GLIBCXX", options.maxGlibcxx],
  ];
  for (const [family, ceiling] of ceilings) {
    const version = required.get(family);
    if (version === undefined) {
      continue;
    }
    findings.push(
      compareVersions(version, ceiling) <= 0
        ? { level: "pass", text: `needs at most ${family}_${version} (ceiling ${ceiling})` }
        : {
            level: "fail",
            text:
              `needs ${family}_${version}, above the ${ceiling} the prebuilt promises. ` +
              "Build inside the manylinux container.",
          }
    );
  }

  return findings;
}

// --- Mach-O (macOS) ----------------------------------------------------------

const MACHO_CPUS: Record<number, string> = {
  0x01000007: "x86_64",
  0x0100000c: "arm64",
};

const LC_SYMTAB = 0x02;
const LC_LOAD_DYLIB = 0x0c;
const LC_LOAD_WEAK_DYLIB = 0x80000018;
const LC_VERSION_MIN_MACOSX = 0x24;
const LC_BUILD_VERSION = 0x32;

function formatMachoVersion(packed: number): string {
  return `${packed >>> 16}.${(packed >>> 8) & 0xff}.${packed & 0xff}`;
}

function verifyMacho(buf: Buffer, options: Options): Finding[] {
  const findings: Finding[] = [];
  const magic = buf.readUInt32LE(0);
  if (magic === 0xbebafeca || magic === 0xbfbafeca) {
    return [
      {
        level: "fail",
        text: "universal (fat) binary - the packages ship one architecture per addon",
      },
    ];
  }
  if (magic !== 0xfeedfacf) {
    return [{ level: "fail", text: "unrecognized object format (not PE, ELF64 or Mach-O 64)" }];
  }

  const cpuType = buf.readUInt32LE(4);
  const commandCount = buf.readUInt32LE(16);
  const cpuName = MACHO_CPUS[cpuType] ?? `0x${cpuType.toString(16)}`;
  console.log(`  format: Mach-O 64, cpu ${cpuName}`);

  const dylibs: string[] = [];
  let deploymentTarget: string | null = null;
  let exportsNapiEntry = false;

  let cursor = 32;
  for (let i = 0; i < commandCount; i++) {
    const command = buf.readUInt32LE(cursor);
    const commandSize = buf.readUInt32LE(cursor + 4);
    if (command === LC_LOAD_DYLIB || command === LC_LOAD_WEAK_DYLIB) {
      dylibs.push(readCString(buf, cursor + buf.readUInt32LE(cursor + 8)));
    } else if (command === LC_BUILD_VERSION) {
      deploymentTarget = formatMachoVersion(buf.readUInt32LE(cursor + 12));
    } else if (command === LC_VERSION_MIN_MACOSX && deploymentTarget === null) {
      deploymentTarget = formatMachoVersion(buf.readUInt32LE(cursor + 8));
    } else if (command === LC_SYMTAB) {
      const symbolOffset = buf.readUInt32LE(cursor + 8);
      const symbolCount = buf.readUInt32LE(cursor + 12);
      const stringOffset = buf.readUInt32LE(cursor + 16);
      for (let symbol = 0; symbol < symbolCount; symbol++) {
        const name = readCString(buf, stringOffset + buf.readUInt32LE(symbolOffset + symbol * 16));
        // Mach-O prefixes C symbols with an underscore.
        if (name === `_${NAPI_ENTRY}`) {
          exportsNapiEntry = true;
          break;
        }
      }
    }
    cursor += commandSize;
  }

  console.log(`  linked dylibs: ${dylibs.join(", ") || "(none)"}`);
  console.log(`  deployment target: ${deploymentTarget ?? "(unset)"}`);

  findings.push(
    exportsNapiEntry
      ? { level: "pass", text: `exports ${NAPI_ENTRY}` }
      : { level: "fail", text: `does not export ${NAPI_ENTRY}` }
  );

  const hostDylibs = dylibs.filter(isHostRuntime);
  findings.push(
    hostDylibs.length === 0
      ? {
          level: "pass",
          text: "no link-time dependency on Node - napi_* symbols resolve from the host at load",
        }
      : {
          level: "fail",
          text: `links against the host runtime (${hostDylibs.join(", ")}), which Electron does not provide`,
        }
  );

  if (deploymentTarget === null) {
    findings.push({ level: "warn", text: "no deployment target recorded in the binary" });
  } else {
    findings.push(
      compareVersions(deploymentTarget, options.maxMacos) <= 0
        ? {
            level: "pass",
            text: `deployment target ${deploymentTarget} (ceiling ${options.maxMacos})`,
          }
        : {
            level: "fail",
            text:
              `deployment target ${deploymentTarget} is newer than ${options.maxMacos}, so the ` +
              "addon refuses to load on macOS releases the supported Electron versions still run on.",
          }
    );
  }

  return findings;
}

// --- entry point -------------------------------------------------------------

function parseOptions(argv: string[]): { files: string[]; options: Options } {
  const options: Options = {
    maxGlibc: "2.28",
    maxGlibcxx: "3.4.25",
    maxMacos: "11.0",
    allowDynamicCrt: false,
  };
  const files: string[] = [];
  for (const argument of argv) {
    if (argument === "--allow-dynamic-crt") {
      options.allowDynamicCrt = true;
    } else if (argument.startsWith("--max-glibcxx=")) {
      options.maxGlibcxx = argument.slice("--max-glibcxx=".length);
    } else if (argument.startsWith("--max-glibc=")) {
      options.maxGlibc = argument.slice("--max-glibc=".length);
    } else if (argument.startsWith("--max-macos=")) {
      options.maxMacos = argument.slice("--max-macos=".length);
    } else if (argument.startsWith("-")) {
      throw new Error(`unknown option: ${argument}`);
    } else {
      files.push(argument);
    }
  }
  return { files, options };
}

function main(): void {
  const { files, options } = parseOptions(process.argv.slice(2));
  if (files.length === 0) {
    console.error(
      "usage: verify-addon.js <addon> [more addons...] " +
        "[--max-glibc=2.28] [--max-glibcxx=3.4.25] [--max-macos=11.0] [--allow-dynamic-crt]"
    );
    process.exit(2);
  }

  let failed = false;
  for (const file of files) {
    console.log(`\n${file}`);
    const buf = readFileSync(file);
    let findings: Finding[];
    if (buf.length > 2 && buf[0] === 0x4d && buf[1] === 0x5a) {
      findings = verifyPe(buf, options);
    } else if (buf.length > 4 && buf.toString("ascii", 1, 4) === "ELF") {
      findings = verifyElf(buf, options);
    } else {
      findings = verifyMacho(buf, options);
    }
    for (const finding of findings) {
      console.log(`  ${finding.level.toUpperCase()}: ${finding.text}`);
      if (finding.level === "fail") {
        failed = true;
      }
    }
  }

  console.log(failed ? "\nverify-addon: FAILED" : "\nverify-addon: OK");
  process.exit(failed ? 1 : 0);
}

main();
