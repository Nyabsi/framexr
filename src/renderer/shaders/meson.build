glslang = find_program('glslangValidator')

shader_spirv = []
foreach shader : [
  ['CommonVertex', 'vert'],
  ['ColorFragment', 'frag'],
  ['LineFragment', 'frag'],
  ['GlyphFragment', 'frag'],
  ['SpriteFragment', 'frag'],
  ['SpriteTintFragment', 'frag'],
  ['SpriteHighlightFragment', 'frag'],
  ['GridFragment', 'frag'],
  ['ScreenFragment', 'frag'],
  ['SrgbFragment', 'frag'],
  ['SwapchainFragment', 'frag'],
]
  shader_spirv += custom_target(
    shader[0] + '.spv',
    input: shader[0] + '.hlsl',
    output: shader[0] + '.spv',
    command: [glslang, '-V', '-D', '-S', shader[1], '-e', 'main', '-o', '@OUTPUT@', '@INPUT@'],
  )
endforeach

shader_embed_dir = import('fs').relative_to(meson.current_build_dir(), meson.global_build_root())
