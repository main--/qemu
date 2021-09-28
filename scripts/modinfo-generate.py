#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import sys

def print_array(name, values):
    if len(values) == 0:
        return
    list = ", ".join(values)
    print("    .%s = ((const char*[]){ %s, NULL })," % (name, list))

def parse_line(line):
    kind = ""
    data = ""
    get_kind = False
    get_data = False
    for item in line.split():
        if item == "MODINFO_START":
            get_kind = True
            continue
        if item.startswith("MODINFO_END"):
            get_data = False
            continue
        if get_kind:
            kind = item
            get_kind = False
            get_data = True
            continue
        if get_data:
            data += " " + item
            continue
    return (kind, data)

def generate(name, lines, core_modules):
    arch = ""
    objs = []
    deps = []
    opts = []
    for line in lines:
        if line.find("MODINFO_START") != -1:
            (kind, data) = parse_line(line)
            if kind == 'obj':
                objs.append(data)
            elif kind == 'dep':
                deps.append(data)
            elif kind == 'opts':
                opts.append(data)
            elif kind == 'arch':
                arch = data;
            elif kind == 'kconfig':
                # don't add a module which dependency is not enabled
                # in kconfig
                if data.strip() not in core_modules:
                    print("    /* module {} isn't enabled in Kconfig. */"
                          .format(data.strip()))
                    print("/* },{ */")
                    return []
            else:
                print("unknown:", kind)
                exit(1)

    print("    .name = \"%s\"," % name)
    if arch != "":
        print("    .arch = %s," % arch)
    print_array("objs", objs)
    print_array("deps", deps)
    print_array("opts", opts)
    print("},{")
    return deps

def print_pre():
    print("/* generated by scripts/modinfo-generate.py */")
    print("#include \"qemu/osdep.h\"")
    print("#include \"qemu/module.h\"")
    print("const QemuModinfo qemu_modinfo[] = {{")

def print_post():
    print("    /* end of list */")
    print("}};")

def main(args):
    if len(args) < 3 or args[0] != '--devices':
        print('Expected: modinfo-generate.py --devices '
              'config-device.mak [modinfo files]', file=sys.stderr)
        exit(1)

    # get all devices enabled in kconfig, from *-config-device.mak
    enabled_core_modules = set()
    with open(args[1]) as file:
        for line in file.readlines():
            config = line.split('=')
            if config[1].rstrip() == 'y':
                enabled_core_modules.add(config[0][7:]) # remove CONFIG_

    deps = {}
    print_pre()
    for modinfo in args[2:]:
        with open(modinfo) as f:
            lines = f.readlines()
        print("    /* %s */" % modinfo)
        (basename, _) = os.path.splitext(modinfo)
        deps[basename] = generate(basename, lines, enabled_core_modules)
    print_post()

if __name__ == "__main__":
    main(sys.argv[1:])
