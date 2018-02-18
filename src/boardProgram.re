let vertexSource = {|
    precision mediump float;
    attribute vec2 position;
    uniform mat3 mat;
    varying vec2 vPosition;
    void main() {
        vPosition = position;
        vec3 transformed = vec3(position, 1.0) * mat;
        gl_Position = vec4(transformed.xy, 0.0, 1.0);
    }
|};

let fragmentSource = {|
    precision mediump float;
    varying vec2 vPosition;

    uniform sampler2D tiles;
    uniform sampler2D sdfTiles;

    const int numCols = 12;
    const int numRows = 26;

    void main() {
        vec2 tilePos = vec2((vPosition.x + 1.0) * 0.5, (vPosition.y - 1.0) * -0.5);
        float tile = texture2D(tiles, tilePos).x;
        int colorIdx = int(tile * 255.0);
        vec3 color =
            (colorIdx == 1) ? vec3(0.5, 0.9, 1.0)
            : (colorIdx == 2) ? vec3(0.45, 0.5, 0.95)
            : (colorIdx == 3) ? vec3(0.95, 0.85, 0.3)
            : (colorIdx == 4) ? vec3(0.95, 1.0, 0.5)
            : (colorIdx == 5) ? vec3(0.55, 0.95, 0.45)
            : (colorIdx == 6) ? vec3(0.7, 0.4, 0.85)
            : (colorIdx == 7) ? vec3(0.9, 0.4, 0.35)
            : vec3(0.8, 0.8, 0.9);
        vec2 sdfPos = vec2(tilePos.x, (tilePos.y * -1.0) + 1.0);
        vec3 sdfColor = texture2D(sdfTiles, sdfPos).xyz;
        float sdfCoef = abs(0.5 - sdfColor.x);
        float tileCoef = 1.0 - sdfCoef;
        //color = color * 0.5 + ((colorIdx == 0) ? vec3(0.0, 0.0, 0.0) : (sdfColor * 0.5));
        gl_FragColor = (colorIdx == 0) ? vec4(0.0, 0.0, 0.0, 0.0) : vec4(color * tileCoef + sdfColor * sdfCoef, 1.0);
    }
|};

let currElVertex = {|
    precision mediump float;
    attribute vec2 position;
    uniform vec2 translation;
    varying vec2 vPosition;
    uniform mat3 mat;
    void main() {
        vPosition = position + translation;
        vec3 transformed = vec3(vPosition, 1.0) * mat;
        gl_Position = vec4(transformed.xy, 0.0, 1.0);
    }
|};
let currElFragment = {|
    precision mediump float;
    varying vec2 vPosition;

    uniform vec3 elColor;
    uniform sampler2D sdfTiles;

    void main() {
        // To texture coords
        vec2 sdfPos = (vPosition + 1.0) * 0.5;
        vec3 sdfColor = texture2D(sdfTiles, sdfPos).xyz;
        float sdfCoef = abs(0.5 - sdfColor.x) * 0.9;
        float tileCoef = 1.0 - sdfCoef;
        vec3 color = elColor * tileCoef + sdfColor * sdfCoef;
        gl_FragColor = vec4(color, 1.0);
    }
|};

module BlinkRows {
    type blinkState =
      | NotBlinking
      | Blinking
      | JustBlinked;

    type t = {
        mutable state: blinkState,
        mutable drawn: bool,
        mutable rows: array(int),
        mutable elapsed: float
    };

    let make = () => {
        {
            state: NotBlinking,
            drawn: false,
            rows: [||],
            elapsed: 0.0
        }
    };

    let endBlink = (self) => {
        self.state = JustBlinked;
        self.drawn = false;
        self.elapsed = 0.0;
    };
};

type t = {
    tiles: array(int),
    mutable updateTiles: bool,
    mutable currElTiles: array(float),
    mutable updateCurrEl: bool,
    mutable boardQuad: Gpu.VertexBuffer.t,
    mutable boardIndexes: Gpu.IndexBuffer.t,
    tilesDraw: Gpu.DrawState.t,
    currElDraw: Gpu.DrawState.t,
    gridDraw: Gpu.DrawState.t,
    colorDraw: ColorDraw.t,
    tileBeam: TileBeam.t,
    canvas: Gpu.Canvas.t,
    tileShadows: TileShadows.t,
    blinkRows: BlinkRows.t,
    rowsDone: int,
    background: Background.t,
    uiBox: SdfNode.inited,
    mutable doneInitDraw: bool
};

open Gpu;

let currElVertices = VertexBuffer.make(
    [|-0.5, -0.5, -0.5, 0.5, 0.5, 0.5, 0.5, -0.5|],
    [|VertexAttrib.make("position", Vec2f)|],
    DynamicDraw
);
let currElIndexes = IndexBuffer.make(IndexBuffer.makeQuadsData(1), DynamicDraw);

let init = (canvas : Gpu.Canvas.t, tiles) => {
    let context = canvas.context;
    /* Background */
    let background = Background.init(canvas);
    FontDraw.loadFont("MasterOfComics", canvas, background.drawState);
    let boardCoords = Coords.getBoardCoords(canvas);
    let boardQuad = VertexBuffer.makeQuad(());
    let boardIndexes = IndexBuffer.makeQuad();
    /* Sdf tiles */
    let sdfTiles = SdfTiles.make(canvas, Light.ProgramLight.default());
    /*SdfTiles.createCanvas();*/
    SdfTiles.drawToTexture(sdfTiles);
    /* Tile beam program */
    let tileBeam = TileBeam.init(canvas);
    /* Tiles texture */
    let tilesTexture = Texture.make(IntDataTexture(Some(tiles), 12, 26), Texture.Luminance, Texture.NearestFilter);
    /* Tiles shadow */
    let tileShadows = TileShadows.init(canvas, boardCoords, tilesTexture);
    /* Grid */
    let gridDraw = GridProgram.createDrawState(
        canvas,
        boardCoords,
        tilesTexture,
        tileShadows.blurTex2,
        tileBeam.beams
    );
    let lineColor = Color.fromFloats(0.15, 0.2, 0.3);
    Uniform.setVec3f(gridDraw.program.uniforms[0].uniform, Color.toVec3(lineColor));
    /* Board program drawState */
    let tilesDraw = DrawState.init(
        canvas.context,
        Program.make(
            Shader.make(vertexSource),
            Shader.make(fragmentSource),
            [|Uniform.make("mat", UniformMat3f(ref(boardCoords.mat)))|]
        ),
        boardQuad,
        Some(boardIndexes),
        [|
            ProgramTexture.make(
                "tiles",
                tilesTexture
            ),
            ProgramTexture.make("sdfTiles", sdfTiles.texture)
        |]
    );
    open Data;
    let currElDraw = DrawState.init(
        canvas.context,
        Program.make(
            Shader.make(currElVertex),
            Shader.make(currElFragment),
            [|
                Uniform.make("elColor", UniformVec3f(ref(Vec3.make(1.0, 0.0, 1.0)))),
                Uniform.make("translation", UniformVec2f(ref(Vec2.zeros()))),
                Uniform.make("mat", UniformMat3f(ref(boardCoords.mat)))
            |]
        ),
        currElVertices,
        Some(currElIndexes),
        [|
            ProgramTexture.make("sdfTiles", sdfTiles.texture)
        |]
    );
    let colorDraw = ColorDraw.init(canvas, boardCoords);

    module M3 = Data.Mat3;
    let boxTrans = M3.trans(0.6, 0.0);
    let boxScale = M3.scale(0.13, 0.2);
    let boxModel = M3.matmul(boxTrans, boxScale);
    /*let uiBox = UiBox.make(canvas, boxModel);*/
    let uiBox = UiBox.makeSdfProgram(canvas, boxModel, Light.ProgramLight.default(), ~color=SdfNode.SdfStaticColor(Color.fromFloats(0.5, 0.6, 0.9)), ());
    let self = {
        tiles,
        currElTiles: [||],
        updateCurrEl: false,
        boardQuad,
        boardIndexes,
        tilesDraw,
        currElDraw,
        gridDraw,
        colorDraw,
        tileBeam,
        canvas,
        updateTiles: false,
        tileShadows,
        blinkRows: BlinkRows.make(),
        rowsDone: 0,
        background,
        uiBox,
        doneInitDraw: false
    };
    self
};

let drawBackground = (self) => {
    Background.draw(self.background);
    SdfNode.draw(self.uiBox);
};

let initDraw = (self) => {
    drawBackground(self);
};

let onResize = (self) => {
    let (width, height) = Gpu.Canvas.getViewportSize();
    /* Todo: There is some bugs here, maybe we need some time after resize
        before we should redraw background */
    Canvas.resize(self.canvas, width, height);
    /* Transform matrices needs update */
    /* Things like this would be nice to structure better */
    let boardCoords = Coords.getBoardCoords(self.canvas);
    let screenUniform = Data.Vec2.make(boardCoords.pixelWidth, boardCoords.pixelHeight);
    Uniform.setMat3f(self.tilesDraw.program.uniforms[0].uniform, boardCoords.mat);
    Uniform.setMat3f(self.currElDraw.program.uniforms[2].uniform, boardCoords.mat);
    Uniform.setMat3f(self.colorDraw.drawState.program.uniforms[1].uniform, boardCoords.mat);
    Uniform.setVec2f(self.gridDraw.program.uniforms[3].uniform, screenUniform);
    Uniform.setMat3f(self.gridDraw.program.uniforms[4].uniform, boardCoords.mat);
    drawBackground(self);
};

let drawScene = (self) => {
    let context = self.canvas.context;
    if (!self.doneInitDraw) {
        initDraw(self);
    };
    /*Canvas.clear(self.canvas, 0.0, 0.0, 0.0);*/
    DrawState.draw(self.gridDraw, self.canvas);
    Gl.enable(~context, Constants.blend);
    Gl.blendFunc(~context, Constants.src_alpha, Constants.one_minus_src_alpha);
    DrawState.draw(self.tilesDraw, self.canvas);
    Gl.disable(~context, Constants.blend);
    DrawState.draw(self.currElDraw, self.canvas);
};

let updateTiles = (self) => {
    if (self.updateTiles) {
        self.tilesDraw.textures[0].texture.update = true;
        /* Update shadows when tiles change */
        TileShadows.draw(self.tileShadows);
        self.updateTiles = false;
    };
    if (self.updateCurrEl) {
        self.currElDraw.vertexBuffer.data = self.currElTiles;
        self.currElDraw.vertexBuffer.update = true;
        switch (self.currElDraw.indexBuffer) {
        | Some(indexBuffer) => {
            /* Four per quad, 2 per element */
            indexBuffer.data = IndexBuffer.makeQuadsData(Array.length(self.currElTiles) / 4 / 2);
            indexBuffer.update = true;
        }
        | None => ()
        }
    };
};

let draw = (self) => {
    let context = self.canvas.context;
    switch (self.blinkRows.state) {
    | BlinkRows.NotBlinking  =>
        updateTiles(self);
        drawScene(self);
    | BlinkRows.Blinking =>
        /* Update tiles is not called, so we have the state before
           as far as rendering is conserned. This is a little brittle */
        if (!self.blinkRows.drawn) {
            drawScene(self);
            Gl.enable(~context, Constants.blend);
            Gl.blendFunc(~context, Constants.src_alpha, Constants.one_minus_src_alpha);
            let rowHeight = 2.0 /. 26.0;
            let vertices = Array.fold_left((vertices, rowIdx) => {
                Array.append(vertices, [|
                    -1.0, 1.0 -. rowHeight *. float_of_int(rowIdx + 1),
                    -1.0, 1.0 -. rowHeight *. float_of_int(rowIdx),
                    1.0, 1.0 -. rowHeight *. float_of_int(rowIdx),
                    1.0, 1.0 -. rowHeight *. float_of_int(rowIdx + 1),
                |]);
            }, [||], self.blinkRows.rows);
            ColorDraw.updateVertices(self.colorDraw, vertices);
            ColorDraw.draw(self.colorDraw, [|1.0, 1.0, 1.0, 0.3|]);
            Gl.disable(~context, Constants.blend);
            self.blinkRows.drawn = true;
        };
        self.blinkRows.elapsed = self.blinkRows.elapsed +. self.canvas.deltaTime;
        if (self.blinkRows.elapsed > 0.6) {
            BlinkRows.endBlink(self.blinkRows);
            /* Game/rendering will continue */
        };
    | BlinkRows.JustBlinked => ()
    };
};