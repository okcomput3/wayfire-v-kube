#include "image-background.hpp"
#include <wayfire/img.hpp>

wf_cube_image_background::~wf_cube_image_background()
{
    wf::gles::run_in_context_if_gles([&]
    {
        GL_CALL(glDeleteTextures(1, &texture));
    });
}

void wf_cube_image_background::reload_texture()
{
    const std::string path = image_path;
    if (path == loaded_path)
    {
        return;
    }

    loaded_path = path;
    GL_CALL(glDeleteTextures(1, &texture));
    texture = 0;
    if (path.empty())
    {
        return;
    }

    GL_CALL(glActiveTexture(GL_TEXTURE0));
    GL_CALL(glGenTextures(1, &texture));
    GL_CALL(glBindTexture(GL_TEXTURE_2D, texture));
    if (image_io::load_from_file(path, GL_TEXTURE_2D))
    {
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    } else
    {
        LOGE("vertical_kube: failed to load background image: ", path);
        GL_CALL(glDeleteTextures(1, &texture));
        texture = 0;
    }

    GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
}

void wf_cube_image_background::render_frame(const wf::render_target_t& target,
    wf_cube_animation_attribs&)
{
    reload_texture();
    OpenGL::clear(fallback_color);
    if (texture)
    {
        OpenGL::render_texture(wf::gles_texture_t{texture}, target, target.geometry,
            glm::vec4(1.0f), OpenGL::TEXTURE_TRANSFORM_INVERT_Y);
    }
}
