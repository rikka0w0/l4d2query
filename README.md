# L4D2Query

A simple tool that allow you to monitor the status of any L4D2 servers 
without the game itself. If query succeeded, This tool prints the server name, current map, 
number of player online and total number of available slots. __Supports Chinese!!!__

Compile the source: `gcc l4d2query.c -o l4d2query`, supports Linux, Windows, Android ...

Syntax
------
`l4d2query hostname[:port]`

__hostname__ is the ipaddress or domain name of the L4D2 server.

__port__ is optional, if not specified, the default port is 27015.

Sample Output
-------------
```
$ gcc l4d2query.c -o l4d2query
$ ./l4d2query 139.162.30.4
Testing: 139.162.30.4:27015
Lewd4Dead! Nano: c13m1_alpinecreek (5/12)
$ ./l4d2query 139.162.30.4:27016
Testing: 139.162.30.4:27016
Lewd4Dead! Singapore: c9m2_lots (4/12)
$ ./l4d2query motherfvcker.261day.com:27015
Testing: 180.97.220.48:27015
地灵殿: c1m1_hotel (3/8)
$ ./l4d2query motherfvcker.261day.com
Testing: 180.97.220.48:27015
地灵殿: c1m1_hotel (3/8)
```