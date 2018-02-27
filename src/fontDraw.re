let vertexSource = {|
    precision mediump float;
    attribute vec2 position;
    attribute vec2 uv;

    uniform mat3 model;
    uniform mat3 layout;
    uniform vec2 pixelSize;

    varying vec2 vUv;
    varying float smoothFactor;

    void main() {
        vUv = uv;
        // x scale. I got the value of this by outputting smoothfactor and sampling color
        // Divided by some factors that looked good to see what I needed
        // to multiply with.
        // Took rise over run with smoothFactor as run and factor as rise,
        // subtracted x - x*slope to get + constant.
        smoothFactor = model[0][0] * pixelSize.x;
        smoothFactor = (smoothFactor * -1.6211904762 + 15.4452380953);
        vec2 pos = vec3(vec3(position, 1.0) * model * layout).xy;
        gl_Position = vec4(pos, 0.0, 1.0);
    }
|};

/* https://github.com/libgdx/libgdx/wiki/Distance-field-fonts */
let fragmentSource = {|
    #ifdef GL_OES_standard_derivatives
    #extension GL_OES_standard_derivatives : enable
    #endif
    precision mediump float;
    uniform sampler2D map;
    uniform vec3 color;
    uniform float opacity;
    varying vec2 vUv;
    varying float smoothFactor;

    float aastep(float value) {
      #ifdef GL_OES_standard_derivatives
        float afwidth = length(vec2(dFdx(value), dFdy(value))) * 0.70710678118654757;
      #else
        float afwidth = (1.0 / 32.0) * (1.4142135623730951 / (2.0 * gl_FragCoord.w));
      #endif
      //afwidth = afwidth * 22.0;
      afwidth = afwidth * smoothFactor;
      // 1.0 - 5.0
      // 0.3 - 22.0
      return smoothstep(0.5 - afwidth, 0.5 + afwidth, value);
    }

    void main() {
        float discardLimit = 0.0001;
        vec4 texColor = 1.0 - texture2D(map, vUv);
        float alpha = aastep(texColor.x);
        //color = texColor.xyz;
        float colorCoef = 1.0 - alpha;
        vec3 c = color * alpha;
        gl_FragColor = vec4(c, opacity * alpha);
        //gl_FragColor = vec4(smoothFactor, 0.0, 0.0, opacity * alpha);
        if (alpha < discardLimit) {
            discard;
        }
    }
|};

let msdfVertexSource =
  String.trim(
    {|#version 300 es
    precision mediump float;
    attribute vec2 position;
    attribute vec2 uv;
    varying vec2 vUv;

    //uniform mat3 model;

    void main() {
        vUv = uv;
        //vec2 pos = vec3(model * vec3(position, 1.0)).xy;
        vec2 pos = position;
        gl_Position = vec4((pos - vec2(40.0, 0.0)) / 50., 0.0, 1.0);
    }
|}
  );

let msdfFragmentSource =
  String.trim(
    {|#version 300 es
    #ifdef GL_OES_standard_derivatives
    #extension GL_OES_standard_derivatives : enable
    #endif
    precision mediump float;
    uniform sampler2D map;
    varying vec2 vUv;

    float median(float r, float g, float b) {
        return max(min(r, g), min(max(r, g), b));
    }

    void main() {
        float opacity = 0.5;
        vec3 color = vec3(0.2, 0.5, 0.8);
        vec3 sample = 1.0 - texture2D(map, vUv).rgb;
        float sigDist = median(sample.r, sample.g, sample.b) - 0.5;
        float alpha = clamp(sigDist/fwidth(sigDist) + 0.5, 0.0, 1.0);
        gl_FragColor = vec4(color.xyz, alpha * opacity);
    }
|}
  );

open Gpu;

type t = {
  mutable text: FontText.block,
  blockInfo: FontText.blockInfo,
  vertices: VertexBuffer.t,
  indices: IndexBuffer.t,
  uModel: Scene.sceneUniform,
  vo: Scene.sceneVertexObject,
  textures: list(Gpu.Texture.t),
  aspect: float,
  fontLayout: FontText.FontLayout.t,
  multicolor: bool
};

let makeText = (
  text : FontText.block,
  fontLayout : FontText.FontLayout.t,
  ~height=0.5,
  ()
) => {
  let blockInfo = FontText.getBlockInfo(text);
  let textures =
    List.map(
      (font) => FontStore.getTexture(fontLayout.store, font),
      blockInfo.fonts
    );
  let multicolor = (List.length(blockInfo.colors) > 1) ? true : false;
  let vertices =
    VertexBuffer.make(
      [||],
      [|
        VertexAttrib.make("position", GlType.Vec2f),
        VertexAttrib.make("uv", GlType.Vec2f)
      |],
      DynamicDraw
    );
  let indices = IndexBuffer.make([||], DynamicDraw);
  let uModel = Scene.UMat3f.id();
  let aspect = 1.0 /. height;
  let vo = Scene.SceneVO.make(vertices, Some(indices));
  {
    text,
    blockInfo,
    vertices,
    indices,
    uModel,
    vo,
    textures,
    aspect,
    fontLayout,
    multicolor
  }
};

let makeSimpleText =
  (
    text,
    font,
    fontLayout,
    ~height=0.2,
    ~align=FontText.Left,
    ~color=Color.fromFloats(1.0, 1.0, 1.0),
    ()
  ) => {
  makeText(
    FontText.block(
      ~height,
      ~font,  
      ~color,
      ~align,
      ~children=[
        FontText.text(text)
      ],
      ()
    ),
    fontLayout,
    ()
  )
};

let updateNode =
  (
    fontDraw : t,
    node : Scene.node('s)
  ) => {
  node.loading = true;
  /* Callback will trigger in function if font is already loaded */
  FontStore.requestMultiple(
    fontDraw.fontLayout.store,
    fontDraw.blockInfo.fonts,
    store => {
      /*let vertices = FontText.Layout.layoutVertices(fontDraw.fontLayout, fontDraw.text);*/
      let vertices = Array.make(0, 0.0);
      VertexBuffer.setDataT(fontDraw.vertices, vertices);
      IndexBuffer.setDataT(
        fontDraw.indices,
        IndexBuffer.makeQuadsData(Array.length(vertices) / 16)
      );
      let modelMat =
        Data.Mat3.matmul(
          Data.Mat3.trans(0.0, 0.0),
          Data.Mat3.scale(1.0, fontDraw.aspect)
        );
      Uniform.setMat3f(fontDraw.uModel.uniform, modelMat);
      node.loading = false;
    }
  );
};

let makeNode =
    (
      fontDraw,
      ~key=?,
      ~cls="fontDraw",
      ~opacity=1.0,
      ~hidden=false,
      ()
    ) => {
  let color =
    switch fontDraw.blockInfo.colors {
    | [color] => Some(color)
    | _ => None
    };
  let uniforms =
      switch color {
      | Some(color) =>
        [
          ("model", fontDraw.uModel),
          ("color", Scene.UVec3f.vec(Color.toVec3(color))),
          ("opacity", Scene.UFloat.make(opacity))
        ]
      | None =>
        [
          ("model", fontDraw.uModel),
          ("opacity", Scene.UFloat.make(opacity))
        ]
      };
  let textures =
    List.mapi(
      (i, texture) => {
        if (i == 0) {
          ("map", Scene.SceneTex.tex(texture))
        } else {
          ("map" ++ string_of_int(i + 1), Scene.SceneTex.tex(texture))
        }
      },
      fontDraw.textures
    );
  let node =
    Scene.makeNode(
      ~key?,
      ~cls,
      ~vertShader=Shader.make(vertexSource),
      ~fragShader=Shader.make(fragmentSource),
      ~textures,
      ~vo=fontDraw.vo,
      ~uniforms,
      ~pixelSizeUniform=true,
      ~transparent=true,
      ~loading=true,
      ~size=Scene.Aspect(fontDraw.aspect),
      ~hidden,
      ()
    );
    updateNode(fontDraw, node);
  node;
};

let makeSimpleNode = (
    text,
    font,
    fontLayout,
    ~height=0.2,
    ~align=FontText.Left,
    ~color=Color.fromFloats(1.0, 1.0, 1.0),
    ~key=?,
    ~cls=?,
    ~opacity=1.0,
    ~hidden=false,
    ()
) => {
  makeNode(
    makeSimpleText(
      text,
      font,
      fontLayout,
      ~height,
      ~align,
      ~color,
      ()
    ),
    ~key=?key,
    ~cls=?cls,
    ~opacity,
    ~hidden,
    ()
  )
};