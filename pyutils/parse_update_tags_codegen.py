#!/usr/bin/env python

import sys
from dataclasses import dataclass

@dataclass
class Tag:
    name: str
    entries: list[int]

@dataclass
class TaggedRegistry:
    name: str
    tags: list[Tag]

@dataclass
class UpdateTags:
    registries: list[TaggedRegistry]


def read_v32(data: bytes, offset: int) -> tuple[bytes, int]:
    byte = 0
    value = 0
    position = 0
    while True:
        if offset >= len(data):
            raise EOFError
        byte = data[offset]
        offset += 1
        value |= (byte & 0x7F) << position
        if byte & 0x80 == 0:
            break
        position += 7
        if position > 31:
            raise ValueError('VarInt too big')
    return value, offset


def read_string(data: bytes, offset: int) -> tuple[str, int]:
    length, offset = read_v32(data, offset)
    if offset + length > len(data):
        raise EOFError
    s = data[offset:offset+length].decode('utf-8', 'replace')
    return s, offset + length


def parse_update_tags(data: bytes, offset: int) -> UpdateTags:
    registry_count, offset = read_v32(data, offset)
    registries = []

    for _ in range(registry_count):
        reg_name, offset = read_string(data, offset)
        tag_count, offset = read_v32(data, offset)
        tags = []
        for _ in range(tag_count):
            tag_name, offset = read_string(data, offset)
            entry_count, offset = read_v32(data, offset)
            entries = []
            for _ in range(entry_count):
                val, offset = read_v32(data, offset)
                entries.append(val)
            tags.append(Tag(tag_name, entries))
        registries.append(TaggedRegistry(reg_name, tags))

    return UpdateTags(registries)


def main():
    try:
        file_name = sys.argv[1]
    except IndexError:
        print(f'Usage: {sys.argv[0]} file', file=sys.stderr)
        sys.exit(1)

    with open(file_name, 'rb') as file:
        data = file.read()

    packet = parse_update_tags(data, 1)

    print('fwd(UpdateTags{std::tuple{')
    for reg in packet.registries:
        print('    TaggedRegistry{"' + reg.name + '", std::tuple{')
        for tag in reg.tags:
            print('        Tag{"' + tag.name + '", std::to_array<int32_t>({')
            print('            ' + ', '.join(str(e) for e in tag.entries))
            print('        })},')
        print('    }},')
    print('}}.put(data_output_it));')

if __name__ == "__main__":
    main()

