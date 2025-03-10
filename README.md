# Ansel

[TOC]

## What is it ?

__Ansel__ is a better future for Darktable, designed from real-life use cases and solving actual problems,
by the guy who did the scene-referred workflow and spent these past 4 years working full-time on Darktable.

It is forked on Darktable 4.0, and is compatible with editing histories produced with Darktable 4.0 and earlier.
It is not compatible with Darktable 4.2 and later and will not be, since 4.2 introduces irresponsible choices that
will be the burden of those who commited them to maintain, and 4.4 will be even worse.

The end goal is :

1. to produce a more robust and faster software, with fewer opportunities for weird, contextual bugs
that can't be systematically reproduced, and therefore will never be fixed,
2. to break with the trend of making Darktable a Vim editor for image processing, truly usable
only from (broken) keyboard shortcuts known only by the hardcore geeks that made them,
3. to sanitize the code base in order to reduce the cost of maintenance, now and in the future,
4. to make the general UI nicer to people who don't have a master's in computer science and
more efficient to use for people actually interested in photography, especially for folks
using Wacom (and other brands) graphic tablets,
5. to optimize the GUI to streamline the scene-referred workflow and make it feel more natural.

Ultimately, the future of Darktable is [vkdt](https://github.com/hanatos/vkdt/), but
this will be available only for computers with GPU and is a prototype that will not be usable by a general
audience for the next years to come. __Ansel__ aims at sunsetting __Darktable__ with something "finished",
pending a VKDT version usable by common folks.

Ansel was forked from Darktable at commit 7b88fdd7afe7b8530a992ae3c12e7a088dc9e992.

`cloc --git --diff 7b88fdd7afe7b8530a992ae3c12e7a088dc9e992 HEAD` returns (output truncated to relevant languages):

```
github.com/AlDanial/cloc v 2.02  T=228.44 s (3.3 files/s, 5236.4 lines/s)
--------------------------------------------------------------------------------
Language                      files          blank        comment           code
--------------------------------------------------------------------------------
C
 same                             0              0          22928         208021
 modified                       266              0            964           7059
 added                           19           2072           2955          18146
 removed                         48           6747           5243          51264
C/C++ Header
 same                             0              0           5018          11746
 modified                        92              0            195            476
 added                           17            599            857           2565
 removed                          9            296            704           2487
C++
 same                             0              0            791           6746
 modified                         8              0             20            170
 added                            1            321            280           2210
 removed                          1            318            243           2020
OpenCL
 same                             0              0            650           4253
 modified                         7              0             43            170
 added                            0             35             28            199
 removed                          0              0              6             35
CSS
 same                             0              0              0              0
 modified                         0              0              0              0
 added                            3            324            357           1794
 removed                         13            419            519           9575
...
--------------------------------------------------------------------------------
SUM:
 same                             0              0          56961         491408
 modified                       486              0         111210          48354
 added                           88          15173          42295         121201
 removed                        186          25494          96370         187755
--------------------------------------------------------------------------------
```

`cloc --git 7b88fdd7afe7b8530a992ae3c12e7a088dc9e992`:

```
github.com/AlDanial/cloc v 2.02  T=3.44 s (337.6 files/s, 388596.5 lines/s)
--------------------------------------------------------------------------------
Language                      files          blank        comment           code
--------------------------------------------------------------------------------
PO File                          36         148288         225050         381427
C                               439          51319          37122         303148
Lua                              13            457            144          28672
C/C++ Header                    284           7286          11522          24848
SVG                              71             66             72          18544
OpenCL                           36           3055           1648          10969
CSS                              20            564            617          10353
C++                              11           1489           1138           9899
HTML                              3           3944             11           8993
JSON                              2              0              0           8775
JavaScript                        8           1513            593           4869
CMake                            94           1047           1236           4813
XML                              10              0              0           4312
Bourne Shell                     29            528            490           2153
Perl                              6            461            202           1832
Markdown                         17            676              4           1711
Ruby                             17            269            168           1354
YAML                              9             37             50           1093
XSLT                              4            175             35           1074
Bourne Again Shell                8            209            137           1010
Python                            7            150            148            876
TeX                               3            121             49            355
Text                              5             83              0            286
make                              4             82             43            281
Objective-C++                     1             20             28            203
ERB                               3             17              0            185
diff                             12             34            136            109
Dockerfile                        1             15             39             81
Assembly                          1              6             15             46
SQL                               1              3             31             38
Nix                               1              7              5             32
DTD                               1              0              0             26
awk                               1              4              0             21
Lisp                              1              0              0              3
INI                               1              0              0              2
--------------------------------------------------------------------------------
SUM:                           1160         221925         280733         832393
--------------------------------------------------------------------------------
```

The volume of C code has therefore been reduced by 15%.

## Download and test

The virtual `0.0.0` [pre-release](https://github.com/aurelienpierreeng/ansel/releases/tag/v0.0.0)
contains nightly builds, with Linux `.Appimage` and Windows `.exe`, compiled automatically
each night with the latest code, and containing all up-to-date dependencies.

## OS support

Ansel is developped on Fedora, heavily tested on Ubuntu and fairly tested on Windows.

Mac OS and, to a lesser extent, Windows have known GUI issues that come from using Gtk as
a graphical toolkit. Not much can be done here, as Gtk suffers from a lack of Windows/Mac devs too.
Go and support these projects so they can have more man-hours put on fixing those.

Mac OS support is not active, because :

- the continuous integration (CI) bot for Mac OS breaks on a weekly basis, which needs much more maintenance than Linux or even Windows,
- only 4% of the Darktable user base runs it, and it's unfair to Linux and Windows users to allocate more resources to the minority OS,
- I don't own a Mac box to debug,
- I don't have an (expensive) developer key to sign .dmg packages,
- even Windows is not _that much_ of a PITA to maintain.

A couple of nice guys are trying their best to fix issues on Mac OS in a timely manner. They have jobs, families, hobbies and vacations too, so Mac OS may work or it may not, there is not much I can do about it.

## Useful links

- [User documentation](https://ansel.photos/en/doc/), in particular:
    - [Build and test on Linux](https://ansel.photos/en/doc/install/linux)
    - [Build and test on Windows](https://ansel.photos/en/doc/install/linux)
- [Contributing guidelines](https://ansel.photos/en/contribute/), in particular:
    - [Project organization](https://ansel.photos/en/contribute/organization/)
    - [Translating](https://ansel.photos/en/contribute/translating/)
    - [Coding style](https://ansel.photos/en/contribute/coding-style/)
- [Developer documentation](https://dev.ansel.photos) (__NEW__)
- [Project news](https://ansel.photos/en/news/)
- [Community forum](https://community.ansel.photos/)
- [Matrix chatrooms](https://app.element.io/#/room/#ansel:matrix.org)
- [Support](https://ansel.photos/en/support/)
