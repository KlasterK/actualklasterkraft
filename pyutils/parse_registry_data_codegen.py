#!/usr/bin/env python

import sys

def parse_print_packet(b):
    registry_name_len = b[1]
    registry_name = b[2:registry_name_len + 2]
    b = b[2 + registry_name_len:]
    print('fwd(RegistryData { "' + registry_name.decode('ASCII') + '",')
    print('    std::to_array<RegistryDataEntry>({')
    while 1:
        _, prefix, right = b.partition(b'minecraft:')
        if not prefix: break
        name, _, b = right.partition(b'\x00')
        print('        { "minecraft:' + name.decode('ASCII') + '", { } },')
    print('    }) }')
    print('        .put(data_output_it));')

def main():
    if len(sys.argv) < 2:
        print(f'Usage: {sys.argv[0]} file1 file2 fileN', file=sys.stderr)
        exit(1)

    for filename in sys.argv[1:]:
        with open(filename, 'rb') as file:
            parse_print_packet(file.read())

if __name__ == '__main__':
    main()
