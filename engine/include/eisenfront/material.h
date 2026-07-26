/*
 * Material - the metallic-roughness PBR parameter set (diffuse, normal,
 * metallic, roughness, AO, emissive) as a reusable GPU resource,
 * separate from any Mesh: many meshes can share one Material, and one
 * Mesh can be drawn with different Materials in different scenes. This
 * is the same separation-of-concerns Renderer draws between geometry
 * (RenderCommand.vertex_array) and shading (RenderCommand.shader_program)
 * - Material simply groups "everything a shader needs to shade a
 * surface" behind one bind call.
 *
 * Uniform blocks: every Material owns a uniform buffer (see buffers.h)
 * holding its scalar factors in a std140-compatible layout, bound to a
 * fixed binding point (MATERIAL_UBO_BINDING) that every material shader
 * declares as `layout(std140, binding = 1) uniform MaterialParams { ... }`.
 * Texture slots bind to fixed texture units (MATERIAL_TEXTURE_UNIT_*)
 * so any shader written against this convention works with any
 * Material without per-material texture-unit bookkeeping. A slot left
 * unset falls back to a shared 1x1 default texture (white for
 * diffuse/metallic/roughness/AO/emissive, flat-normal (0.5,0.5,1.0) for
 * normal) so a shader can always sample every slot unconditionally
 * without a texture-missing branch.
 *
 * Material instances: material_instance_create() clones a base
 * Material's textures and parameters into an independently mutable
 * copy that still shares the base's shader. This is the standard "one
 * shared material definition, many per-object tweaks" pattern - many
 * MaterialInstances can point at the same base "brick_wall" Material
 * (same shader, same textures) while each tints base_color slightly
 * differently, without duplicating textures or recompiling shaders.
 *
 * Dependencies: Core, Shader, Texture, Buffers.
 */
#ifndef EISENFRONT_MATERIAL_H
#define EISENFRONT_MATERIAL_H

#include "buffers.h"
#include "core.h"
#include "shader.h"
#include "texture.h"

#define MATERIAL_TEXTURE_UNIT_DIFFUSE 0u
#define MATERIAL_TEXTURE_UNIT_NORMAL 1u
#define MATERIAL_TEXTURE_UNIT_METALLIC 2u
#define MATERIAL_TEXTURE_UNIT_ROUGHNESS 3u
#define MATERIAL_TEXTURE_UNIT_AO 4u
#define MATERIAL_TEXTURE_UNIT_EMISSIVE 5u
#define MATERIAL_UBO_BINDING 1u

ENGINE_API Result material_system_init(void);
ENGINE_API void   material_system_shutdown(void);

typedef struct MaterialTextures {
    Texture *diffuse;   /* nullptr falls back to a shared white texture */
    Texture *normal;    /* nullptr falls back to a shared flat-normal texture */
    Texture *metallic;  /* nullptr falls back to a shared white texture */
    Texture *roughness; /* nullptr falls back to a shared white texture */
    Texture *ao;        /* nullptr falls back to a shared white texture */
    Texture *emissive;  /* nullptr falls back to a shared white texture */
} MaterialTextures;

typedef struct MaterialParams {
    float base_color[4];      /* RGBA */
    float emissive_factor[3]; /* RGB */
    float metallic_factor;
    float roughness_factor;
    float ao_strength;
} MaterialParams;

ENGINE_API MaterialParams material_params_default(void);

typedef struct MaterialDesc {
    ShaderProgram    *shader; /* required; retained */
    MaterialTextures  textures;
    MaterialParams    params;
} MaterialDesc;

typedef struct Material Material;

ENGINE_API Result material_create(const MaterialDesc *desc, Material **out_material);
ENGINE_API void   material_retain(Material *material);
ENGINE_API void   material_release(Material *material);

/* Replaces the material's texture bindings: releases whichever old
 * textures it held and retains the new ones (nullptr slots fall back to
 * the shared defaults, same as at creation). */
ENGINE_API void material_set_textures(Material *material, const MaterialTextures *textures);
ENGINE_API void material_set_params(Material *material, const MaterialParams *params);

/* Binds the shader, all six texture units, and the parameter UBO - the
 * complete "use this material for the next draw" operation. Callers
 * still bind the Mesh's vertex array and issue the draw themselves. */
ENGINE_API void material_bind(const Material *material);

typedef struct MaterialInstance MaterialInstance;

ENGINE_API Result material_instance_create(Material *base, MaterialInstance **out_instance);
ENGINE_API void   material_instance_retain(MaterialInstance *instance);
ENGINE_API void   material_instance_release(MaterialInstance *instance);

ENGINE_API void material_instance_set_textures(MaterialInstance *instance,
                                                const MaterialTextures *textures);
ENGINE_API void material_instance_set_params(MaterialInstance *instance,
                                              const MaterialParams *params);

ENGINE_API void material_instance_bind(const MaterialInstance *instance);

#endif /* EISENFRONT_MATERIAL_H */
