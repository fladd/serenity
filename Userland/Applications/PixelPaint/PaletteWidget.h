/*
 * Copyright (c) 2018-2020, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2021, Felix Rauch <noreply@felixrau.ch>
 * Copyright (c) 2021, Mustafa Quraish <mustafa@cs.toronto.edu>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Result.h>
#include <AK/Vector.h>
#include <LibGUI/Frame.h>

namespace PixelPaint {

class ImageEditor;
class SelectedColorWidget;

class PaletteWidget final : public GUI::Frame {
    C_OBJECT(PaletteWidget);

public:
    virtual ~PaletteWidget() override;

    void set_primary_color(Color);
    void set_secondary_color(Color);

    void display_color_list(Vector<Color> const&);

    Vector<Color> colors();

    static Result<Vector<Color>, String> load_palette_fd_and_close(int);
    static Result<Vector<Color>, String> load_palette_path(String const&);
    static Result<void, String> save_palette_fd_and_close(Vector<Color>, int);
    static Vector<Color> fallback_colors();

    void set_image_editor(ImageEditor&);

private:
    static Result<Vector<Color>, String> load_palette_file(Core::File&);

    explicit PaletteWidget();

    ImageEditor* m_editor { nullptr };
    RefPtr<SelectedColorWidget> m_primary_color_widget;
    RefPtr<SelectedColorWidget> m_secondary_color_widget;
    RefPtr<GUI::Widget> m_color_container;
};

}
