// This file is part of Drawpile.
// Copyright (C) 2021 Calle Laakkonen
//
// Drawpile is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// As additional permission under section 7, you are allowed to distribute
// the software through an app store, even if that store has restrictive
// terms and conditions that are incompatible with the GPL, provided that
// the source is also available under the GPL with or without this permission
// through a channel without those restrictive terms and conditions.
//
// Drawpile is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Drawpile.  If not, see <https://www.gnu.org/licenses/>.

use std::fs::File;
use std::path::Path;

use image::gif::GifDecoder;
use image::io::Reader as ImageReader;
use image::{AnimationDecoder, ImageDecoder};

use super::conv::to_dpimage;
use crate::ImageImportError;
use dpcore::paint::layerstack::{LayerFill, LayerInsertion};
use dpcore::paint::{editlayer, Blendmode, Color, LayerStack, Rectangle};

/// Load a flat image (an image that does not have layers)
pub fn load_flat_image(path: &Path) -> Result<LayerStack, ImageImportError> {
    let img = ImageReader::open(path)?.decode()?;
    let img = to_dpimage(&img.into_rgba8());

    let mut ls = LayerStack::new(img.width as u32, img.height as u32);

    let mut layer = ls
        .add_layer(
            0x0101,
            LayerFill::Solid(Color::TRANSPARENT),
            LayerInsertion::Top,
        )
        .unwrap();
    layer.title = "Layer 1".into();

    editlayer::draw_image(
        &mut layer,
        1,
        &img.pixels,
        &Rectangle::new(0, 0, img.width as i32, img.height as i32),
        1.0,
        Blendmode::Replace,
    );

    Ok(ls)
}

/// Load a (possibly) animated GIF
///
/// A layer will be created for each frame
pub fn load_gif_animation(path: &Path) -> Result<LayerStack, ImageImportError> {
    let decoder = GifDecoder::new(File::open(path)?)?;
    let (w, h) = decoder.dimensions();
    let mut ls = LayerStack::new(w, h);

    for (i, frame) in decoder.into_frames().enumerate() {
        let mut layer = ls
            .add_layer(
                0x0101 + i as u16,
                LayerFill::Solid(Color::TRANSPARENT),
                LayerInsertion::Top,
            )
            .unwrap();
        layer.title = format!("Layer {}", i + 1);

        let frame = frame?;
        let img = to_dpimage(frame.buffer());

        editlayer::draw_image(
            &mut layer,
            1,
            &img.pixels,
            &Rectangle::new(
                frame.left() as i32,
                frame.top() as i32,
                img.width as i32,
                img.height as i32,
            ),
            1.0,
            Blendmode::Replace,
        );
    }

    Ok(ls)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    #[test]
    fn test_load_testpattern() {
        let path: PathBuf = [env!("CARGO_MANIFEST_DIR"), "testdata", "testpattern.png"]
            .iter()
            .collect();
        let ls = load_flat_image(path.as_ref()).unwrap();
        let layer = ls.get_layer(0x0101).unwrap();

        assert_eq!(
            layer.sample_color(32, 32, 0,),
            Color {
                r: 1.0,
                g: 0.0,
                b: 0.0,
                a: 1.0
            }
        );
        assert_eq!(
            layer.sample_color(96, 32, 0,),
            Color {
                r: 0.0,
                g: 1.0,
                b: 0.0,
                a: 1.0
            }
        );
        assert_eq!(
            layer.sample_color(32, 96, 0,),
            Color {
                r: 0.0,
                g: 0.0,
                b: 1.0,
                a: 1.0
            }
        );
        assert_eq!(
            layer.sample_color(96, 96, 0,),
            Color {
                r: 1.0,
                g: 1.0,
                b: 1.0,
                a: 0.502
            }
        );
    }
}
