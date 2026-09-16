
import os

CHUNK = 4096


def write_sparse(path, data):
    """Write `data' to `path', leaving aligned all-zero chunks unallocated."""
    zero = bytes(CHUNK)
    with open(path, 'wb') as f:
        for off in range(0, len(data), CHUNK):
            chunk = data[off:off+CHUNK]
            if len(chunk) == CHUNK and chunk == zero:
                f.seek(CHUNK, os.SEEK_CUR)
            else:
                f.write(chunk)
        # The final seek past the last chunk allocates nothing, so the file
        # would otherwise end at the last byte actually written.  Truncate up
        # to the intended length: a medium one chunk short of its geometry is
        # not the medium.
        f.truncate(len(data))
