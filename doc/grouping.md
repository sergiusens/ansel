# Image grouping feature

[TOC]

- remove image from group before removing/deleting it. [done]
- add duplicates to the same group as the original image. [done]

## 'G' off:

- display all images. [done]
- draw a border around all the images in the group when hovering over an image with the mouse. [done]
- merge border of adjacent images. [done]
- add config option whether border is to be painted or not. [done]
- disable grouping accels (ctrl-g, ctrl-shift-g) so that people don't hide their images where they won't find them again without noticing. [done]

## 'G' on:

- only display a single image ("representative") from each group. [done]
- show the 'G' in a corner of the thumbnail. [done]
- expand the group temporarily when clicking the 'G'. [done]
- when expanding a group while another group is already expanded, then also collapse the latter. [done]
- show the 'G' for all images in a temporarily expanded group. [done]
- draw the border around the images of the temporarily expanded group as if grouping was turned off, just in a different color. [done]
- highlight the 'G' for the representative ... [done]
- ... and when clicking the 'G' of another image in the expanded group, then this image shall become the representative. [done]
- collapse the group when clicking the 'G' of the representative. [done]
- ctrl/shift clicking the 'G' of a group selects all the images in the group. [done for expanded groups]
- when a group is expanded and another image is selected, then this image can be joined to the group by ctrl-g. [done]
- when multiple images are selected and no group is expanded, then ctrl-g will merge them into a new group. [done]
- remove an image with ctrl-shift-g from a group. [done]
- an image is the representative of the group iff id == group_id. [done]
- expand the group after creating a duplicate and adding it. [done]

## TODO:

- rating/rejecting an image does the same to all images in the group iff the group is collapsed.
