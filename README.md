# nginx-lmdb-queue
nginx module to write data into lmdb-queue

## HOW TO BUILD
```sh
# In nginx source path, execute:
./configure --add-module=/path/to/nginx-lmdb-queue
# Nginx configure doesn't configure CXXFLAGS, so we have to this:
mv objs/Makefile objs/Makefile.old; sed 's/\t\t\(nginx-lmdb-queue\/src\/.*\.cc\)/\t\t-std=c++11 \1/' objs/Makefile.old > objs/Makefile
make -j
```