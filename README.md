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

## Code analysis

Ansel was forked from Darktable after commit 7b88fdd7afe7b8530a992ae3c12e7a088dc9e992, 1 month before Darktable 4.0 release
(output truncated to relevant languages):

```
$ cloc --git --diff 7b88fdd7afe7b8530a992ae3c12e7a088dc9e992 HEAD
github.com/AlDanial/cloc v 2.02  T=227.18 s (2.9 files/s, 4772.1 lines/s)
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
CSS
 same                             0              0              0              0
 modified                         0              0              0              0
 added                            3            324            357           1794
 removed                         13            419            519           9575
...
--------------------------------------------------------------------------------
SUM:
 same                             0              0          56961         491412
 modified                       486              0         111210          48363
 added                           88          15173          42295         121274
 removed                        186          25488          96370         187742
--------------------------------------------------------------------------------
```

```
$ cloc --git 7b88fdd7afe7b8530a992ae3c12e7a088dc9e992
github.com/AlDanial/cloc v 2.02  T=3.70 s (286.4 files/s, 327937.4 lines/s)
--------------------------------------------------------------------------------
Language                      files          blank        comment           code
--------------------------------------------------------------------------------
C                               439          51319          37122         303148
C/C++ Header                    284           7286          11522          24848
C++                              11           1489           1138           9899
CSS                              20            564            617          10353
...
--------------------------------------------------------------------------------
SUM:                           1160         221925         280733         832393
--------------------------------------------------------------------------------
```

```
$ cloc --git HEAD
github.com/AlDanial/cloc v 2.02  T=3.70 s (286.4 files/s, 327937.4 lines/s)
--------------------------------------------------------------------------------
Language                      files          blank        comment           code
--------------------------------------------------------------------------------
C                               410          46644          34834         270030
C/C++ Header                    292           7589          11675          24926
C++                              11           1492           1175          10089
CSS                              10            469            455           2572
...
--------------------------------------------------------------------------------
SUM:                           1062         211610         243588         800478
--------------------------------------------------------------------------------
```

The volume of C code has therefore been reduced by 11%, the volume of CSS (for theming)
by 75%. Excluding the pixel operations (`cloc  --fullpath --not-match-d=/src/iop --git`),
the C code volume has reduced by 15%.

The [cyclomatic complexity](https://en.wikipedia.org/wiki/Cyclomatic_complexity) of the project
has also been reduced reduced:

| Metric | Ansel Master | Darktable 4.0 | Darktable 5.0 |
| ------ | -----------: | ------------: | ------------: |
| Cyclomatic complexity | [48830](https://sonarcloud.io/component_measures?metric=complexity&id=aurelienpierreeng_ansel) | [56170](https://sonarcloud.io/component_measures?metric=complexity&id=aurelienpierre_darktable) | [59377](https://sonarcloud.io/component_measures?metric=complexity&id=aurelienpierreeng_darktable-5) |
| Cognitive complexity | [62033](https://sonarcloud.io/component_measures?metric=cognitive_complexity&id=aurelienpierreeng_ansel) | [72743](https://sonarcloud.io/component_measures?metric=cognitive_complexity&id=aurelienpierre_darktable) | [77039](https://sonarcloud.io/component_measures?metric=cognitive_complexity&id=aurelienpierreeng_darktable-5) |
| Lines of code | [318890](https://sonarcloud.io/component_measures?metric=ncloc&id=aurelienpierreeng_ansel) | [361046](https://sonarcloud.io/component_measures?metric=ncloc&id=aurelienpierre_darktable) | [370781](https://sonarcloud.io/component_measures?metric=ncloc&id=aurelienpierreeng_darktable-5) |
| Ratio of comments | [12.3%](https://sonarcloud.io/component_measures?metric=comment_lines_density&id=aurelienpierreeng_ansel) | [11.5%](https://sonarcloud.io/component_measures?metric=comment_lines_density&id=aurelienpierre_darktable) | [11.7%](https://sonarcloud.io/component_measures?metric=comment_lines_density&id=aurelienpierreeng_darktable-5) |

Those figures are indirect indicators of the long-term maintainability of the project:

- comments document the code and are used by Doxygen to build the [dev docs](https://dev.ansel.photos),
- code volume and complexity make bugs harder to find and fix properly, and lead to more cases to cover with tests,
- code volume and complexity prevent from finding optimization opportunities,
- let's remember that it's mostly the same software with pretty much the same features anyway.

Dealing with growing features should be made through modularity, that is splitting the app features into modules,
enclosing modules into their own space, and make modules independent from each other's internals.
We will see below how those "modules" behave (spoiler 1: that this did not happen), (spoiler 2:
feel free to go the [dev docs](https://dev.ansel.photos), where all functions have their
dependency graph in their doc, to witness that "modules" are not even modular, and the whole application
is actually aware of the whole application).

Let's see a comparison of Ansel vs. Dartable 4.0 and 5.0 complexity per file/feature
(figures are: cyclomatic complexity / lines of code excluding comments - lower is better) :


### Pixel pipeline, development history, image manipulation backends

| File | Description | Ansel Master  | Darktable 4.0 | Darktable 5.0 |
| ---- | ----------- | ------------: | ------------: | ------------: |
| `src/common/selection.c` | Database images selection backend | [57](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_ansel%3Asrc%2Fcommon%2Fselection.c&view=list&id=aurelienpierreeng_ansel) / 258 | [62](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierre_darktable%3Asrc%2Fcommon%2Fselection.c&view=list&id=aurelienpierre_darktable) / 342 | [62](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_darktable-5%3Asrc%2Fcommon%2Fselection.c&view=list&id=aurelienpierreeng_darktable-5) / 394 |
| `src/common/act_on.c` | GUI images selection backend | [12](src/common/https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_ansel%3Asrc%2Fcommon%2Fact_on.c&view=list&id=aurelienpierreeng_ansel) / 32 | [80](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierre_darktable%3Asrc%2Fcommon%2Fact_on.c&view=list&id=aurelienpierre_darktable) / 297 | [80](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_darktable-5%3Asrc%2Fcommon%2Fact_on.c&view=list&id=aurelienpierreeng_darktable-5) / 336 |
| `src/common/collection.c` | Image collection extractions from library database | [396](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_ansel%3Asrc%2Fcommon%2Fcollection.c&view=list&id=aurelienpierreeng_ansel) / 1561 | [532](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierre_darktable%3Asrc%2Fcommon%2Fcollection.c&view=list&id=aurelienpierre_darktable) / 2133 | [553](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_darktable-5%3Asrc%2Fcommon%2Fcollection.c&view=list&id=aurelienpierreeng_darktable-5) / 2503 |
| `src/develop/pixelpipe_hb.c` | Pixel pipeline processing backbone | [449](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_ansel%3Asrc%2Fdevelop%2Fpixelpipe_hb.c&view=list&id=aurelienpierreeng_ansel) / 2182 | [473](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierre_darktable%3Asrc%2Fdevelop%2Fpixelpipe_hb.c&view=list&id=aurelienpierre_darktable) / 2013 | [553](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_darktable-5%3Asrc%2Fdevelop%2Fpixelpipe_hb.c&view=list&id=aurelienpierreeng_darktable-5) / 2644 |
| `src/develop/pixelpipe_cache.c` | Pixel pipeline image cache* | [29](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_ansel%3Asrc%2Fdevelop%2Fpixelpipe_cache.c&view=list&id=aurelienpierreeng_ansel) / 153 | [55](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierre_darktable%3Asrc%2Fdevelop%2Fpixelpipe_cache.c&view=list&id=aurelienpierre_darktable) / 242 | [95](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_darktable-5%3Asrc%2Fdevelop%2Fpixelpipe_cache.c&view=list&id=aurelienpierreeng_darktable-5) / 368
| `src/common/mipmap_cache.c` | Lighttable thumbnails cache | [189](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_ansel%3Asrc%2Fcommon%2Fmipmap_cache.c&view=list&id=aurelienpierreeng_ansel) / 1070 | [193](https://sonarcloud.io/component_measures?metric=ncloc&selected=aurelienpierre_darktable%3Asrc%2Fcommon%2Fmipmap_cache.c&id=aurelienpierre_darktable) / 1048 | [225](https://sonarcloud.io/component_measures?metric=ncloc&selected=aurelienpierreeng_darktable-5%3Asrc%2Fcommon%2Fmipmap_cache.c&id=aurelienpierreeng_darktable-5) / 1310 |
| `src/develop/develop.c` | Development history & pipeline backend (original) | - | [600](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierre_darktable%3Asrc%2Fdevelop%2Fdevelop.c&view=list&id=aurelienpierre_darktable) / 2426| [628](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_darktable-5%3Asrc%2Fdevelop%2Fdevelop.c&view=list&id=aurelienpierreeng_darktable-5) / 2732 |
| `src/develop/develop.c` | Development pipeline backend (refactored) | [290](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_ansel%3Asrc%2Fdevelop%2Fdevelop.c&view=list&id=aurelienpierreeng_ansel) / 1059 | - |  - |
| `src/develop/dev_history.c` | Development history backend (refactored from `develop.c`) | [265](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_ansel%3Asrc%2Fdevelop%2Fdev_history.c&view=list&id=aurelienpierreeng_ansel) / 1227 | - | - |
| `src/develop/imageop.c` | Pixel processing module API | [531](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_ansel%3Asrc%2Fdevelop%2Fimageop.c&view=list&id=aurelienpierreeng_ansel) / 2264 | [617](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierre_darktable%3Asrc%2Fdevelop%2Fimageop.c&view=list&id=aurelienpierre_darktable) / 2513 | [692](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_darktable-5%3Asrc%2Fdevelop%2Fimageop.c&view=list&id=aurelienpierreeng_darktable-5) / 3181 |
| `src/control/jobs/control_jobs.c` | Background thread tasks | [315](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_ansel%3Asrc%2Fcontrol%2Fjobs%2Fcontrol_jobs.c&view=list&id=aurelienpierreeng_ansel) / 1962 | [308](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierre_darktable%3Asrc%2Fcontrol%2Fjobs%2Fcontrol_jobs.c&view=list&id=aurelienpierre_darktable) / 1904 | [394](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_darktable-5%3Asrc%2Fcontrol%2Fjobs%2Fcontrol_jobs.c&view=list&id=aurelienpierreeng_darktable-5) / 2475 |


*: to this day, Darktable pixel pipeline cache is still broken as of 5.0, which clearly shows that increasing its complexity was not a solution:
- https://github.com/darktable-org/darktable/issues/18517
- https://github.com/darktable-org/darktable/issues/18133

### GUI

| File | Description | Ansel Master  | Darktable 4.0 | Darktable 5.0 |
| ---- | ----------- | ------------: | ------------: | ------------: |
| `src/bauhaus/bauhaus.c` | Custom Gtk widgets (sliders/comboboxes) for modules | [470](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_ansel%3Asrc%2Fbauhaus%2Fbauhaus.c&id=aurelienpierreeng_ansel) / 2344 | [653](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierre_darktable%3Asrc%2Fbauhaus%2Fbauhaus.c&view=list&id=aurelienpierre_darktable) / 2833 | [751](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_darktable-5%3Asrc%2Fbauhaus%2Fbauhaus.c&view=list&id=aurelienpierreeng_darktable-5) / 3317 |
| `src/gui/accelerators.c` | Key shortcuts handler | [88](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_ansel%3Asrc%2Fgui%2Faccelerators.c&view=list&id=aurelienpierreeng_ansel) / 342 | [1088](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierre_darktable%3Asrc%2Fgui%2Faccelerators.c&view=list&id=aurelienpierre_darktable) / 3546 | [1245](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_darktable-5%3Asrc%2Fgui%2Faccelerators.c&view=list&id=aurelienpierreeng_darktable-5) / 5221
| `src/views/darkroom.c` | Darkroom GUI view | [374](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_ansel%3Asrc%2Fviews%2Fdarkroom.c&view=list&id=aurelienpierreeng_ansel) / 2041 | [736](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierre_darktable%3Asrc%2Fviews%2Fdarkroom.c&view=list&id=aurelienpierre_darktable) / 3558 | [560](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_darktable-5%3Asrc%2Fviews%2Fdarkroom.c&view=list&id=aurelienpierreeng_darktable-5) / 2776
| `src/libs/modulegroups.c` | Groups of modules in darkroom GUI | [149](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_ansel%3Asrc%2Flibs%2Fmodulegroups.c&view=list&id=aurelienpierreeng_ansel) / 608 | [554](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierre_darktable%3Asrc%2Flibs%2Fmodulegroups.c&view=list&id=aurelienpierre_darktable) / 3155 | [564](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_darktable-5%3Asrc%2Flibs%2Fmodulegroups.c&view=list&id=aurelienpierreeng_darktable-5) / 3322 |
| `src/views/lighttable.c` | Lighttable GUI view | [17](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_ansel%3Asrc%2Fviews%2Flighttable.c&view=list&id=aurelienpierreeng_ansel) / 152 | [227](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierre_darktable%3Asrc%2Fviews%2Flighttable.c&view=list&id=aurelienpierre_darktable) / 1002 | [237](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_darktable-5%3Asrc%2Fviews%2Flighttable.c&view=list&id=aurelienpierreeng_darktable-5) / 1007 |
| `src/dtgtk/thumbtable.c` | Lighttable & filmroll grid of thumbnails view | [294](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_ansel%3Asrc%2Fdtgtk%2Fthumbtable.c&view=list&id=aurelienpierreeng_ansel) / 1309 | [533](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierre_darktable%3Asrc%2Fdtgtk%2Fthumbtable.c&view=list&id=aurelienpierre_darktable) / 2146 | [559](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_darktable-5%3Asrc%2Fdtgtk%2Fthumbtable.c&view=list&id=aurelienpierreeng_darktable-5) / 2657
| `src/dtgtk/thumbnail.c` | Lighttable & filmroll thumbnails | [157](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_ansel%3Asrc%2Fdtgtk%2Fthumbnail.c&view=list&id=aurelienpierreeng_ansel) / 924 | [345](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierre_darktable%3Asrc%2Fdtgtk%2Fthumbnail.c&view=list&id=aurelienpierre_darktable) / 1622 | [332](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_darktable-5%3Asrc%2Fdtgtk%2Fthumbnail.c&view=list&id=aurelienpierreeng_darktable-5) / 1841
| `src/libs/tools/filter.c` | Darktable 3.x Lighttable collection filters & sorting (original) | [85](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_ansel%3Asrc%2Flibs%2Ftools%2Ffilter.c&id=aurelienpierreeng_ansel) / 620 | - | - |
| `src/libs/filters` | Darktable 4.x Lighttable collection filters (modules) | - | [453](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierre_darktable%3Asrc%2Flibs%2Ffilters&id=aurelienpierre_darktable) / 2296 | [504](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_darktable-5%3Asrc%2Flibs%2Ffilters&id=aurelienpierreeng_darktable-5) / 2664 |
| `src/libs/filtering.c` | Darktable 4.x Lighttable collection filters (main widget) | - | [245](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierre_darktable%3Asrc%2Flibs%2Ffiltering.c&id=aurelienpierre_darktable) / 1633 | [290](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_darktable-5%3Asrc%2Flibs%2Ffiltering.c&view=list&id=aurelienpierreeng_darktable-5) / 1842
| `src/common/import.c` | File import popup window | [130](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_ansel%3Asrc%2Fcommon%2Fimport.c&view=list&id=aurelienpierreeng_ansel) / 915 | [309](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierre_darktable%3Asrc%2Flibs%2Fimport.c&view=list&id=aurelienpierre_darktable) / 1923 | [334](https://sonarcloud.io/component_measures?metric=complexity&selected=aurelienpierreeng_darktable-5%3Asrc%2Flibs%2Fimport.c&view=list&id=aurelienpierreeng_darktable-5) / 2309 |


## Runtimes

All runtimes computed on a Lenovo Thinkpad P51 laptop (Intel Xeon CPU E3-1505M v6 @ 3.00GHz, Nvidia GPU Quadro M2200 4 GB vRAM, 32 GB RAM, 4K display), CPU in performance mode, Linux Fedora 41 with KDE/Plasma desktop. Pixel pipeline runtimes are not compared since Ansel 0.1-alpha shares its pixel code with Darktable 4.0 by design (compatibility).

| Description | Ansel Master | Darktable 5.0 |
| ----------- | ------------ | ------------- |
| Time from app startup to last lighttable thumbnail drawing (same collection) | 2.66 s | 7.49 s |
| Time to switch from lighttable to darkroom (same image) | 0.2 s | 1.2 s |
| Time to scroll (start->end) the same collection of 471 images (thumbnails preloaded in disk cache) | 3.9 s | 5.0 s |

The following have been measured on battery, in powersave mode, with the application sitting idle (no user interaction) for 5 minutes, using Intel Powertop. The baseline consumption of the whole idle OS is 1.6 % CPU. (Power is given for the app only, % CPU is given for the whole system):

| View | Ansel Master | Darktable 5.0 |
| ---- | ------------ | ------------- |
| Lighttable | 1.8 % CPU, power: 6.8 mW | 2.7 % CPU, power: 103 mW |
| Darkroom   | 1.8 % CPU, power: 10.3 mW | 1.8 % CPU, power: 22 mW |

TL;DR: Darktable is leaking performance by the GUI, and the tedious work done in 2023-2024 on optimizing pixel processing modules for an extra 15-50 ms is completely irrelevant.
