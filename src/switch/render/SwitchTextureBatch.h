#pragma once

// Batched texture sub-image updates for the Switch backend.
//
// Between begin and end, renderTextureSubImageRgba() on level 0 only copies the
// pixels into a CPU mirror of the bound texture and grows that texture's dirty
// rectangle; end uploads each dirty rectangle with one glTexSubImage2D. The
// animated terrain tiles (water, lava, fire, portal) and the compass/clock
// otherwise issue ~15 separate uploads per tick into atlases the GPU is still
// sampling, each of which can make the driver wait for the GPU.
void switchBeginTextureBatch();
void switchEndTextureBatch();
