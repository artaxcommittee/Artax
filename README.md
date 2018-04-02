                   _____ __________________________  ____  ___
                  /  _  \\______   \__    ___/  _  \ \   \/  /
                 /  /_\  \|       _/ |    | /  /_\  \ \     /  2018 Artax/ARTX
                /    |    \    |   \ |    |/    |    \/     \ 
                \____|____/____|___/ |____|\____|____/___/\__\
                                     
E3 - V1.0.0.1 - Artax PoS Masternode Chain

Artax is a Powerful Node-Based Pure PoS-based cryptocurrency.

## Artax Specifications

| Specification | Value |
|:-----------|:-----------|
| Block Spacing | `120 seconds` |
| Stake Minimum Age | `10 hours` |
| Masternode Minimum Age | `24 hours` |
| Port | `21527` |
| RPC Port | `22833` |

## Social Channels

| Site | link |
|:-----------|:-----------|
| Bitcointalk | https://bitcointalk.org/ |
| Twitter | http://twitter.com/ArtaxCoin |
| Discord | https://discord.gg/btMYs43 |
| Website | http://www.artaxcoin.org |



BUILD LINUX
-----------
1) git clone https://github.com/Artax-Project/Artax

2) cd Artax 

3) sudo chmod +x autogen.sh

4) sudo ./autogen.sh

5) sudo ./configure

6) sudo chmod +x share/genbuild.sh

7) sudo make



BUILD WINDOWS
-------------

1) Download Qt.zip from https://github.com/Artax-Project/Artax/releases/tag/v1.0 and unpack to C:/

2) Download Artax source from https://github.com/Artax-Project/Artax/archive/master.zip 

2.1) Unpack to C:/Artax

3) Install Perl for windows from the homepage http://www.activestate.com/activeperl/downloads

4) Download Python 2.7 https://www.python.org/downloads/windows/

4.1) While installing python make sure to add python.exe to the path.

5) Run msys.bat located in C:\MinGW49-32\msys\1.0

6) cd /C/Artax/src/leveldb

7) Type "TARGET_OS=NATIVE_WINDOWS make libleveldb.a libmemenv.a" and hit enter to build leveldb

8) Exit msys shell

9) Open windows command prompt

10) cd C:/dev

11) Type "49-32-qt5.bat" and hit enter to run

12) cd ../Artax

13) Type "qmake USE_UPNP=0" and hit enter to run

14) Type "mingw32-make" and hit enter to start building. When it's finished you can find your .exe in the release folder


For more information please visit the website:

http://artaxcoin.org
