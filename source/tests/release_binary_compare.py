"""Compare identical source builds while ignoring linker timestamps/checksum."""
import sys
import pefile


def normalized(path):
    pe = pefile.PE(path)
    for structure in pe.__structures__:
        if hasattr(structure, 'TimeDateStamp'):
            structure.TimeDateStamp = 0
    pe.OPTIONAL_HEADER.CheckSum = 0
    return pe.write()


assert normalized(sys.argv[1]) == normalized(sys.argv[2]), 'PE content differs beyond timestamps/checksum'
print('Packaged source reproduces the release DLL except PE timestamps/checksum.')
