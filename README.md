This project defines a read-only filesystem that is capable of decompressing and reading the contents of a `.tar.gz` compressed archive.

Dependencies:
-----

- Boost: iostreams
- libfuse: fuse3


Commands:
-----

Building:

```
mkdir build
cd build
cmake ..
make
```

Running:

```
./FS ../archive.tar.gz <SOME_DIR>
```

You must create and supply your own directory to be mounted onto.

`archive.tar.gz` is provided for your convenience, but any compressed tarball
should work.


Proof of Work:
-----

See proof.jpg for command-line output.
