IcBox
==========

Detect IcBox device and process calls

This library IcBox device conected on machine, receive caller id number and send to application as a event


### Building

To compile, you will need docker in your machine

- Windows
  ```cmd
  docker run -ti -v %cd%:/mnt mazinsw/mingw-w64:4.0.4 /bin/sh -c "cd /mnt && make clean shared64 && make clean shared32 && make clean static64 && make clean static32"
  ```

- Linux
  ```cmd
  docker run -ti -v `pwd`:/mnt -u `id -u $USER`:`id -g $USER` mazinsw/mingw-w64:4.0.4 /bin/sh -c 'cd /mnt && make clean shared64 && make clean && make static64 && make clean static32'
  ```
