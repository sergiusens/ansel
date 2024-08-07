# Thumbnails color management

[TOC]

to ease debugging the code will use DT_COLORSPACE_DISPLAY for images loaded without a color space mentioned in Exif or when explicitly exporting
for the Display profile, while buffers get initialized as DT_COLORSPACE_NONE. if ever such a thumbnail gets encountered there is a bug
(i.e., a code path where a thumbnail gets loaded without the color space being set).


## DRAWING/EXPOSE

- thumbnails are tagged as sRGB, AdobeRGB, Display or None
  None means that we got a code path which doesn't set the color space -> BUG
- check config if thumbnails should be color managed
    - no: just dump whatever we have on the screen
    - yes:
        * transform sRGB and AdobeRGB using lcms2 to current display profile, use the others as-is
        * put the resulting pixels on the screen


## WRITING CACHE TO DISK

- thumbnails are tagged as sRGB, AdobeRGB, Display or None
- mark sRGB and AdobeRGB in Exif
- write JPEG


## REQUESTING MIPMAP

- reading thumbnail from disk cache
    - read JPEG
    - check if file is tagged as sRGB or AdobeRGB in Exif
        * yes: tag thumbnail accordingly
        * no: tag thumbnail as Display since that's what we had before as processed thumbnails
- loading embedded thumbnail (in general it's a JPEG but might be TIFF or such like)
    - read data
    - tag thumbnail as sRGB (TODO: verify if that's always true, might be AdobeRGB when the camera was set accordingly?)
- loading full ldr image
    - try to get the color space from the file. this only returns correct results for sRGB or AdobeRGB files at the moment
- processing image
    - image goes through pipe
    - set output profile in colorout's commit_params to AdobeRGB when thumbnail color management is on, use Display otherwise
    - tag resulting thumbnail accordingly



## CONCLUSION

when thumbnail color management is on:
- we export new thumbnails as AdobeRGB
- when reading cached thumbnails they are either new and tagged or old and were exported using the Display profile. they get tagged accordingly
- when displaying thumbnails the tagged ones (sRGB / AdobeRGB) are correctly transformed to the Display profile,
  old thumbnails were exported using the Display profile and are thus also shown correctly (provided the display profile is still correct)
- when writing thumbnails back they are either tagged and we keep that information, or they are untagged and we keep that, too

when thumbnail color management is off:
- we export new thumbnails for the Display profile
- when reading cached thumbnails they might be tagged or not. we remember that information
- when displaying thumbnails we just dump the pixels to the screen. that will result in wrong colors in case of thumbnails tagged as sRGB or
  AdobeRGB. this can happen when the user once had thumbnail color management on, generated some thumbnails and then turned it off again
- when writing thumbnails back they are either tagged and we keep that information, or they are untagged and we keep that, too

there are two cases where colors would be wrong:
- old thumbnails or those created with thumbnail color management turned off are not tagged, so they get displayed wrongly when the display profile
  changed. there isn't much we can do about that, old thumbnails will always have unknown color space, and dealing with display icc profiles is
  probably too expensive for new ones
- thumbnails created with thumbnail color management turned on and then used with it being turned off are not in Display color space but sRGB (when
  read from embedded thumbnails) or AdobeRGB (when created by our pipe). Just dumping these on the screen will give wrong colors. one possible way
  around that would be ditching tagged thumbnails when reading from disk while thumbnail color management is off (i.e., pretending they were not in
  the cache when reading them)


## TODO
what to do when loading a jpeg or other file to create a thumbnail and it's neither sRGB nor AdobeRGB but some random other format? Convert to
AdobeRGB? dump pixels to screen and wait for the full pipe to run eventually to get color managed thumbnails?
