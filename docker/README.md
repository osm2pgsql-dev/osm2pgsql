# Docker based test for local and travis based test

This scripts should run on .travis and at the local docker environment

# system requirements
- docker  ( 17.09.0-ce+ )
- docker-compose ( 1.17.1+ )


# Base docker images
- `mdillon/postgis:10-alpine` (you can replace in the `docker-compose.yml`)
    - https://hub.docker.com/r/mdillon/postgis/tags/   see other versions
        - 9.3-alpine ( small size )
        - 9.4-alpine     - 
        - 9.5-alpine
        - 9.6-alpine
        - 9.6  ( debian based , bigger size  )
        - 9.5 
        - ...
    - download(compressed) size ~44 MB - disk size: 191MB (for each image )
- `gcc:7`   ( or ...  `gcc:4` )    ( see `GCCVER` global parameter )
    - https://hub.docker.com/r/library/gcc/tags/
    - download(compressed) size ~449 MB - disk size: 1.64GB (for each image )
        - `gcc:7`  : the latest gcc7 version - now : 7.3.0
        - `gcc:8`  : the latest gcc8 version - now : 8.1.0
        - `latest` : ...

# Before run!
- expecting a minimal docker, docker-compose experience
- check the `docker-compose.yml`! 
    - volumes, ports, images , default parameters ...
- backup your modifications!  
    - the "./build" directory will be deleted!
    - new "./temp-pgdata" will be created ( postgresql data )
- experimetal ...

# Local Simple test
- check the parameters inside: `./docker/run_gcc7sanitize.sh`
- check your free space  at least ~6Gb for running this test.
- run:   
    - `./docker/run_all.sh`   [ or  `sudo ./docker/run_all.sh` ] 
-   - ( first time - downloading ~500Mb docker images )
- check the logs
- cleaning:
    - stop the postgis server  `docker-compose down`
    - (if you want) remove temp dir : `./temp-pgdata`
    - (if you want) remove extra docker images


# program files

You can run your local dev only this examples 

| files                              |                                           |
|------------------------------------| ----------------------------------------- |
| ./docker/run_all.sh                | Matrix test example                       |
| ./docker/run_gcc4simple.sh         | Simple gcc4 test with DB                  |
| ./docker/run_gcc7dbluajittest.sh   | Simple gcc7 test example -dbteszt+luajit  |
| ./docker/run_gcc7dbtest.sh         | Simple gcc7 test example -dbtest          |
| ./docker/run_gcc7sanitize.sh       | Simple gcc7 test example -sanitize        |

Other programs, dirs 

| files                              |                                            | 
|------------------------------------| -------------------------------------------|
| docker-compose.yml                 | config files, please check!                | 
| .travis.yml                        | travis test config                         |
|                                 | ...                                        |
| ./docker/build.sh                  | build ( cmake, make, ctest )               |
| ./docker/docker_build_gcc_image.sh | creating `osm2pgsql-dev:gcc7` image        |
| ./docker/Dockerfile-osm2pgsql-dev  | Dockerfile for `osm2pgsql-dev:gcc7` image  |
| ./docker/info.sh                   | print debug info from the container        |
| ./docker/tablespace.sh             | Postgis tablespace initialisation          |
| ./docker/travis_test.sh            | main test code - without parameters!       |
|                                    | ...                                        |
| ./temp-pgdata/* .....              | Temporary postgres data, you can remove!   |
| ./build/*                          | cmake build directory, will be overwrite!  |



# Global Parameters ( for travis & local dev )

you can modify only:

*  `.travis.yml`           - `env:` value 
*  `./docker/run_gcc7sanitize.sh`
*  `./docker/run_gcc7dbluajittest.sh`
*  `./docker/run_gcc7dbtest.sh`



Parameters:

* `$GCCVER` : gcc docker image tag 
    examples: `7`,`7.1`,`7.2`,`8`
    valid values:  https://hub.docker.com/r/library/gcc/tags/

* `$CXXFLAGS` : gcc option
    * current limitations:  should not be empty
        * `CXXFLAGS="-pedantic -Werror -fsanitize=address"`
        * `CXXFLAGS="-Ofast"`
        * `CXXFLAGS="-Werror"`
    *  `CXXFLAGS=""` : it is not working!!! find a dummy option


* `$LUA_OPTION`: cmake option       
    * valid values: `ON`,`OFF`
    * injected to "cmake ....  -DWITH_LUA=${LUA_OPTION}"

* `$LUAJIT_OPTION`: cmake option       
    * valid values: `ON`,`OFF`
    * injected to "cmake ....  -DWITH_LUAJIT=${LUAJIT_OPTION}"

* `$RUNTEST`  : cmake test  
   * code injected to ->  `ctest -VV ${RUNTEST}`
    *  `RUNTEST="#All"`    -> all tests with db 
    *  `RUNTEST="-L NoDB"` -> testing without db  
   * Known limitations: no option for disable testing 



# other limitations
- `make -j2` hard coded in the `./docker/build.sh`  for the travis
- postgis version is hard coded in the `docker-compose.yml` 
- ...


# Cookbook

### check the extra docker images

```bash
$ docker images
REPOSITORY         TAG           IMAGE ID        CREATED         SIZE
mdillon/postgis    10-alpine     876a5dfc34b8    6 days ago      191MB
osm2pgsql-dev      gcc7          0c924812db7e    16 hours ago    1.81GB
gcc                7             888a86905abb    4 weeks ago     1.64GB
```

### Check the running Postgis server

```bash
$ docker ps
CONTAINER ID        IMAGE                       COMMAND                  CREATED             STATUS              PORTS                       NAMES
a07d7eedd42a        mdillon/postgis:10-alpine   "docker-entrypoint.s…"   About an hour ago   Up About an hour    127.0.0.1:25433->5432/tcp   osm2pgsql_postgis_1
```
you can connect to  "127.0.0.1:25433"  (see the actual pasword in the `docker-compose.yml` )


### Stop the postgis server

```bash
$ docker-compose down
WARNING: The RUNTEST variable is not set. Defaulting to a blank string.
Stopping osm2pgsql_postgis_1 ... done
Removing osm2pgsql_postgis_1 ... done
Removing network osm2pgsql_default
```


### Local Matrix testing:
- check the parameters inside: `./docker/run_all.sh`
- check your free space 
- run:   `./docker/run_all.sh`
- see the logs ..


