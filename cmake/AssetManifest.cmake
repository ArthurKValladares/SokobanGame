# One source of truth for runtime model assets and their rendering metadata.
# Fields are pipe-delimited so paths containing spaces remain single CMake items.

set(SOKOBAN_MODEL_ASSETS
    "BricksA|assets/KayKit Block Bits 1.0/Assets/gltf/bricks_A.gltf|models/bricks_A.gltf|Static|false|false|false|Untextured|-"
    "Stone|assets/KayKit Block Bits 1.0/Assets/gltf/stone.gltf|models/stone.gltf|Static|false|false|false|Untextured|-"
    "Water|assets/KayKit Block Bits 1.0/Assets/gltf/water.gltf|models/water.gltf|Static|false|false|false|Untextured|-"
    "Glass|assets/KayKit Block Bits 1.0/Assets/gltf/glass.gltf|models/glass.gltf|Static|false|false|false|Untextured|-"
    "Conveyor|assets/KayKit Platformer Pack 1.0/Assets/gltf/blue/conveyor_4x4x1_blue.gltf|models/conveyor_4x4x1_blue.gltf|Static|false|false|true|PrimitiveTextureIndex|0"
    "Rogue|assets/KayKit Adventurers 2.0/Characters/gltf/Rogue.glb|models/Rogue.glb|Skinned|true|true|false|SingleTexture|Rogue"
)

# List order is the Vulkan descriptor-array index. Primitive material indices
# may address this array directly, so keep Platformer and PlatformerThread at
# indices 1 and 2 for the current conveyor asset.
set(SOKOBAN_TEXTURE_ASSETS
    "Rogue|assets/KayKit Adventurers 2.0/Characters/gltf/rogue_texture.png|models/rogue_texture.png"
    "Platformer|assets/KayKit Platformer Pack 1.0/Assets/gltf/blue/platformer_texture.png|models/platformer_texture.png"
    "PlatformerThread|assets/KayKit Platformer Pack 1.0/Assets/gltf/blue/threads.png|models/threads.png"
)

set(SOKOBAN_ANIMATION_ASSETS
    "RogueIdle|assets/KayKit Adventurers 2.0/Animations/gltf/Rig_Medium/Rig_Medium_General.glb|models/Rig_Medium_General.glb|playerIdleAnimationNumber"
    "RogueMovement|assets/KayKit Adventurers 2.0/Animations/gltf/Rig_Medium/Rig_Medium_MovementBasic.glb|models/Rig_Medium_MovementBasic.glb|playerMovementAnimationNumber"
    "RoguePush|assets/custom/Rig_Medium_Push.glb|models/Rig_Medium_Push.glb|playerPushAnimationNumber"
)

set(SOKOBAN_MODEL_SUPPORT_ASSETS
    "assets/KayKit Block Bits 1.0/Assets/gltf/bricks_A.bin|models/bricks_A.bin"
    "assets/KayKit Block Bits 1.0/Assets/gltf/stone.bin|models/stone.bin"
    "assets/KayKit Block Bits 1.0/Assets/gltf/water.bin|models/water.bin"
    "assets/KayKit Block Bits 1.0/Assets/gltf/glass.bin|models/glass.bin"
    "assets/KayKit Platformer Pack 1.0/Assets/gltf/blue/conveyor_4x4x1_blue.bin|models/conveyor_4x4x1_blue.bin"
)

function(sokoban_manifest_fields entry output)
    string(REPLACE "|" ";" fields "${entry}")
    set(${output} "${fields}" PARENT_SCOPE)
endfunction()

function(sokoban_append_copy source destination)
    set(source_path "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
    get_filename_component(destination_directory "${destination}" DIRECTORY)
    list(APPEND SOKOBAN_ASSET_COPY_COMMANDS
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/assets/${destination_directory}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${source_path}" "${CMAKE_CURRENT_BINARY_DIR}/assets/${destination}"
    )
    list(APPEND SOKOBAN_ASSET_DEPENDENCIES "${source_path}")
    set(SOKOBAN_ASSET_COPY_COMMANDS "${SOKOBAN_ASSET_COPY_COMMANDS}" PARENT_SCOPE)
    set(SOKOBAN_ASSET_DEPENDENCIES "${SOKOBAN_ASSET_DEPENDENCIES}" PARENT_SCOPE)
endfunction()

set(SOKOBAN_ASSET_COPY_COMMANDS)
set(SOKOBAN_ASSET_DEPENDENCIES)
set(SOKOBAN_GENERATED_MODEL_ENTRIES)
set(SOKOBAN_GENERATED_ANIMATION_ENTRIES)
set(SOKOBAN_GENERATED_TEXTURE_ENTRIES)

foreach(entry IN LISTS SOKOBAN_TEXTURE_ASSETS)
    sokoban_manifest_fields("${entry}" fields)
    list(GET fields 0 name)
    list(GET fields 1 source)
    list(GET fields 2 destination)
    sokoban_append_copy("${source}" "${destination}")
    string(APPEND SOKOBAN_GENERATED_TEXTURE_ENTRIES
        "        TextureAssetDefinition { \"${name}\", \"${destination}\" },\n")
endforeach()

foreach(entry IN LISTS SOKOBAN_MODEL_ASSETS)
    sokoban_manifest_fields("${entry}" fields)
    list(GET fields 0 model)
    list(GET fields 1 source)
    list(GET fields 2 destination)
    list(GET fields 3 geometry)
    list(GET fields 4 preserve_aspect_ratio)
    list(GET fields 5 rotate_half_turn)
    list(GET fields 6 primitive_textures)
    list(GET fields 7 material_mode)
    list(GET fields 8 material_texture)
    sokoban_append_copy("${source}" "${destination}")

    set(texture_index 0)
    if(material_mode STREQUAL "SingleTexture")
        set(texture_index -1)
        set(candidate_index 0)
        foreach(texture_entry IN LISTS SOKOBAN_TEXTURE_ASSETS)
            sokoban_manifest_fields("${texture_entry}" texture_fields)
            list(GET texture_fields 0 texture_name)
            if(texture_name STREQUAL material_texture)
                set(texture_index ${candidate_index})
                break()
            endif()
            math(EXPR candidate_index "${candidate_index} + 1")
        endforeach()
        if(texture_index LESS 0)
            message(FATAL_ERROR "Unknown texture '${material_texture}' for model ${model}")
        endif()
    elseif(material_mode STREQUAL "PrimitiveTextureIndex")
        set(texture_index ${material_texture})
    endif()

    string(APPEND SOKOBAN_GENERATED_MODEL_ENTRIES
        "        ModelAssetDefinition { RenderModel::${model}, \"${destination}\", ModelGeometry::${geometry}, { ${preserve_aspect_ratio}, ${rotate_half_turn}, ${primitive_textures} }, ModelMaterialMode::${material_mode}, ${texture_index} },\n")
endforeach()

foreach(entry IN LISTS SOKOBAN_ANIMATION_ASSETS)
    sokoban_manifest_fields("${entry}" fields)
    list(GET fields 0 animation)
    list(GET fields 1 source)
    list(GET fields 2 destination)
    list(GET fields 3 animation_number_setting)
    sokoban_append_copy("${source}" "${destination}")
    string(APPEND SOKOBAN_GENERATED_ANIMATION_ENTRIES
        "        AnimationAssetDefinition { RenderAnimation::${animation}, \"${destination}\", config::${animation_number_setting} },\n")
endforeach()

foreach(entry IN LISTS SOKOBAN_MODEL_SUPPORT_ASSETS)
    sokoban_manifest_fields("${entry}" fields)
    list(GET fields 0 source)
    list(GET fields 1 destination)
    sokoban_append_copy("${source}" "${destination}")
endforeach()

list(LENGTH SOKOBAN_MODEL_ASSETS SOKOBAN_MODEL_ASSET_COUNT)
list(LENGTH SOKOBAN_TEXTURE_ASSETS SOKOBAN_MODEL_TEXTURE_COUNT)
list(LENGTH SOKOBAN_ANIMATION_ASSETS SOKOBAN_ANIMATION_ASSET_COUNT)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/engine/render")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GeneratedAssetCatalog.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/engine/render/GeneratedAssetCatalog.hpp"
    @ONLY
)
