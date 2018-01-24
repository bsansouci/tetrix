module Document = {
  type window;
  let window: window = [%bs.raw "window"];
  [@bs.send] external addEventListener : ('window, string, 'eventT => unit) => unit = "addEventListener";
};

[@bs.set] external setLastKeyCode : ('a, int) => unit = "__lastKeyCode";
[@bs.get] external lastKeyCode : 'a => int = "__lastKeyCode";
[@bs.get] external getWhich : 'eventT => int = "which";

let tickDuration = 0.5;
let elColorOffset = 2;
let boardOffsetX = 50;
let boardOffsetY = 20;
let tileCols = 12;
let tileRows = 26;
let tileWidth = 20;
let tileHeight = 20;
let tilePadding = 3;
let boardWidth = tileCols * tileWidth;

type element =
  | Cube
  | Line
  | Triangle
  | RightTurn
  | LeftTurn
  | LeftL
  | RightL
  ;

type inputAction =
  | None
  | MoveLeft
  | MoveRight
  | MoveDown
  | BlockLeft
  | BlockRight
  | CancelDown
  | DropDown
  | RotateCW
  | RotateCCW
  | MoveBeginning
  | MoveEnd
  | Pause
  ;

let getTetronimo = (element) => {
  switch element {
  | Cube => Tetronimo.cubeTiles
  | Line => Tetronimo.lineTiles
  | Triangle => Tetronimo.triangleTiles
  | RightTurn => Tetronimo.rightTurnTiles
  | LeftTurn => Tetronimo.leftTurnTiles
  | LeftL => Tetronimo.leftLTiles
  | RightL => Tetronimo.rightLTiles
  }
};

let elTiles = (element, rotation) => {
  let tetronimo = getTetronimo(element);
  switch rotation {
  | 1 => tetronimo.points90
  | 2 => tetronimo.points180
  | 3 => tetronimo.points270
  | _ => tetronimo.points
  }
};

let tileColors2 = Array.map((color) => {
  Array.map((component) => {
    float_of_int(component) /. 255.0
  }, color)
},
[|
  [|199, 214, 240, 255|], /* Standard unfilled color */
  [|205, 220, 246, 255|], /* Standard lighter color */
  [|130, 240, 250, 255|], /* Magenta line */
  [|120, 130, 250, 255|], /* Blue left L */
  [|250, 210, 80, 255|], /* Orange right L */
  [|250, 250, 130, 255|], /* Yellow cube */
  [|140, 250, 140, 255|], /* Green right shift */
  [|180, 100, 230, 255|], /* Purple triangle */
  [|240, 130, 120, 255|], /* Red left shift */
|]);

type pos = {
  x: int,
  y: int
};

type curEl = {
  el: element,
  pos: pos,
  color: int,
  rotation: int
};

type gameState =
  | Running
  | GameOver;

let beamNone = -2;

type stateT = {
  action: inputAction,
  curEl: curEl,
  posChanged: bool,
  rotateChanged: bool,
  lastTick: float,
  curTime: float,
  tiles: array(array(int)),
  beams: array((int, int)),
  gameState: gameState,
  paused: bool,
  boardProgram: BoardProgram.t,
  lastCompletedRows: array(int)
};


let newElement = () => {
  let newElType = switch (Random.int(7)) {
  | 0 => Cube
  | 1 => Line
  | 2 => Triangle
  | 3 => RightTurn
  | 4 => LeftTurn
  | 5 => LeftL
  | 6 => RightL
  | _ => Cube
  };
  let tetronimo = getTetronimo(newElType);
  {
    el: newElType,
    color: tetronimo.colorIndex,
    rotation: 0,
    pos: {
      x: tileCols / 2,
      y: 2
    }
  }
};
let updateBeams = (state) => {
  /* Reset element tile rows */
  Array.iteri((i, (tileRow, _toRow)) => {
    if (tileRow > beamNone) {
      state.beams[i] = (beamNone, 0);
    }
  }, state.beams);
  /* Set row where element tile is */
  List.iter(((x, y)) => {
    let pointX = state.curEl.pos.x + x;
    let pointY = state.curEl.pos.y + y;
    let (beamFrom, beamTo) = state.beams[pointX];
    if (beamFrom < pointY) {
      state.beams[pointX] = (pointY, beamTo);
    };
  }, elTiles(state.curEl.el, state.curEl.rotation));
  /* Set end of beam */
  /* This could almost be cached, but there are edge cases
     where tile is navigated below current beamTo.
     Could make update when moved below */
  Array.iteri((i, (beamFrom, _beamTo)) => {
    if (beamFrom > beamNone) {
      let beamTo = ref(0);
      for (j in beamFrom to tileRows - 1) {
        if (beamTo^ == 0) {
          if (state.tiles[j][i] > 0) {
            beamTo := j;
          };
        };
      };
      if (beamTo^ == 0) {
        beamTo := tileRows;
      };
      state.beams[i] = (beamFrom, beamTo^);
    };
  }, state.beams);
  /* Create vertices for beams */
  let rowHeight = 2.0 /. float_of_int(tileRows);
  let colWidth = 2.0 /. float_of_int(tileCols);
  let (_, vertices) = Array.fold_left(((i, vertices), (beamFrom, beamTo)) => {
    if (beamFrom > beamNone) {
      let beamVertices = [|
        -1.0 +. float_of_int(i) *. colWidth,
        1.0 -. (float_of_int(beamTo) *. rowHeight +. rowHeight),
        -1.0 +. float_of_int(i) *. colWidth,
        1.0 -. float_of_int(beamFrom) *. rowHeight,
        -1.0 +. (float_of_int(i) *. colWidth +. colWidth),
        1.0 -. float_of_int(beamFrom) *. rowHeight,
        -1.0 +. (float_of_int(i) *. colWidth +. colWidth),
        1.0 -. (float_of_int(beamTo) *. rowHeight +. rowHeight),
      |];
      (i + 1, Array.append(vertices, beamVertices))
    } else {
      (i + 1, vertices)
    };
  }, (0, [||]), state.beams);
  let elColor = tileColors2[state.curEl.color];
  state.boardProgram.tileBeam.drawState.uniforms[0] = Gpu.Uniform.UniformVec3f(elColor);
  TileBeam.updateVertices(state.boardProgram.tileBeam, vertices, state.boardProgram.canvas);
};

let setup = (canvas) : stateT => {
  Document.addEventListener(
    Document.window,
    "keydown",
    (e) => {
      setLastKeyCode(Document.window, getWhich(e))
    }
  );
  Random.self_init();
  let tiles = Array.make(tileRows * tileCols, 0);
  let bp = BoardProgram.init(canvas, tiles);
  /*Mandelbrot.createCanvas();*/
  /*let sdf = SdfTiles.createCanvas();
  SdfTiles.draw(sdf);*/
  let state = {
    action: None,
    boardProgram: bp,
    curEl: newElement(),
    posChanged: true,
    rotateChanged: true,
    lastTick: 0.,
    curTime: 0.,
    tiles: Array.make_matrix(tileRows, tileCols, 0),
    beams: Array.make(tileCols, (beamNone, 0)),
    gameState: Running,
    paused: false,
    lastCompletedRows: [||]
  };
  state.boardProgram.updateTiles = true;
  state
};

let newGame = (state) => {
  for (y in 0 to tileRows - 1) {
    for (x in 0 to tileCols - 1) {
      state.tiles[y][x] = 0;
      state.boardProgram.tiles[tileCols*y + x] = 0;
    }
  };
  state.boardProgram.updateTiles = true;
  {
    ...state,
    action: None,
    curEl: newElement(),
    posChanged: true,
    rotateChanged: true,
    lastTick: 0.,
    curTime: 0.,
    gameState: Running
  }
};

let isCollision = (state) => {
  List.exists(((tileX, tileY)) => {
    state.curEl.pos.y + tileY >= tileRows
    || state.curEl.pos.x + tileX < 0 || state.curEl.pos.x + tileX > tileCols - 1
    || state.tiles[state.curEl.pos.y + tileY][state.curEl.pos.x + tileX] > 0
  }, elTiles(state.curEl.el, state.curEl.rotation))
};

let attemptMove = (state, (x, y)) => {
  let moved = {
    ...state,
    posChanged: true,
    curEl: {
      ...state.curEl,
      pos: {
        x: state.curEl.pos.x + x,
        y: state.curEl.pos.y + y
      }
    }
  };
  (isCollision(moved)) ? state : moved
};
let attemptMoveTest = (state, (x, y)) => {
  let moved = {
    ...state,
    posChanged: true,
    curEl: {
      ...state.curEl,
      pos: {
        x: state.curEl.pos.x + x,
        y: state.curEl.pos.y + y
      }
    }
  };
  (isCollision(moved)) ? (false, state) : (true, moved)
};

/* Wall kicks http://tetris.wikia.com/wiki/SRS */
let wallTests = (state, newRotation, positions) => {
  let rec loop = (positions) => {
    switch (positions) {
    | [] => (false, state)
    | [(x, y), ...rest] => {
      let rotated = {
        ...state,
        rotateChanged: true,
        curEl: {
          ...state.curEl,
          rotation: newRotation,
          pos: {
            x: state.curEl.pos.x + x,
            y: state.curEl.pos.y - y,
          }
        }
      };
      if (isCollision(rotated)) {
        loop(rest)
      } else {
        (true, rotated)
      }
    }
    }
  };
  loop(positions)
};
let attemptRotateCW = (state) => {
  let newRotation = (state.curEl.rotation + 1) mod 4;
  /* First test for successful default rotation */
  let rotated = {
    ...state,
    rotateChanged: true,
    curEl: {
      ...state.curEl,
      rotation: newRotation
    }
  };
  if (!isCollision(rotated)) {
    (true, rotated)
  } else {
    /* Loop wall kick tests */
    let testPositions = switch (state.curEl.el) {
    | Line => switch (newRotation) {
    | 1 => [(-2, 0), (1, 0), (-2, -1), (1, 2)]
    | 2 => [(-1, 0), (2, 0), (-1, 2), (2, -1)]
    | 3 => [(2, 0), (-1, 0), (2, 1), (-1, -2)]
    | 0 => [(1, 0), (-2, 0), (1, -2), (-2, 1)]
    | _ => []
    }
    | _ => switch (newRotation) {
    | 1 => [(-1, 0), (-1, 1), (0, -2), (-1, -2)]
    | 2 => [(1, 0), (1, -1), (0, 2), (1, 2)]
    | 3 => [(1, 0), (1, 1), (0, -2), (1, -2)]
    | 0 => [(-1, 0), (-1, -1), (0, 2), (-1, 2)]
    | _ => []
    }
    };
    wallTests(state, newRotation, testPositions)
  }
};

let attemptRotateCCW = (state) => {
  let newRotation = (state.curEl.rotation == 0) ? 3 : (state.curEl.rotation - 1);
  /* First test for successful default rotation */
  let rotated = {
    ...state,
    rotateChanged: true,
    curEl: {
      ...state.curEl,
      rotation: newRotation
    }
  };
  if (!isCollision(rotated)) {
    (true, rotated)
  } else {
    /* Loop wall kick tests */
    let testPositions = switch (state.curEl.el) {
    | Line => switch (newRotation) {
    | 1 => [(1, 0), (-2, 0), (1, -2), (-2, 1)]
    | 2 => [(-2, 0), (1, 0), (-2, -1), (1, 2)]
    | 3 => [(-1, 0), (2, 0), (-1, 2), (2, -1)]
    | 0 => [(2, 0), (-1, 0), (2, 1), (-1, -2)]
    | _ => []
    }
    | _ => switch (newRotation) {
    | 1 => [(-1, 0), (-1, 1), (0, -2), (-1, -2)]
    | 2 => [(-1, 0), (-1, -1), (0, 2), (-1, 2)]
    | 3 => [(1, 0), (1, 1), (0, -2), (1, -2)]
    | 0 => [(1, 0), (1, -1), (0, 2), (1, 2)]
    | _ => []
    }
    };
    wallTests(state, newRotation, testPositions)
  }
};

let elToTiles = (state) => {
  List.iter(((tileX, tileY)) => {
    let posy = state.curEl.pos.y + tileY;
    let posx = state.curEl.pos.x + tileX;
    state.tiles[state.curEl.pos.y + tileY][state.curEl.pos.x + tileX] = state.curEl.color;
    state.boardProgram.tiles[posy * tileCols + posx] = state.curEl.color - 1;
  }, elTiles(state.curEl.el, state.curEl.rotation));
  state.boardProgram.updateTiles = true;
};

let listRange = (countDown) => {
  let rec addToList = (list, countDown) => {
    if (countDown <= 0) {
      list
    } else {
      addToList([countDown, ...list], countDown - 1)
    }
  };
  addToList([], countDown)
};

let processAction = (state) => {
  switch state.action {
  | MoveLeft  => {
    ...attemptMove(state, (-1, 0)),
    action: None
  }
  | MoveRight => {
    ...attemptMove(state, (1, 0)),
    action: None
  }
  | BlockLeft => {
    ...List.fold_left((state, _) => attemptMove(state, (-1, 0)), state, listRange(3)),
    action: None
  }
  | BlockRight => {
    ...List.fold_left((state, _) => attemptMove(state, (1, 0)), state, listRange(3)),
    action: None
  }
  | MoveBeginning => {
    ...List.fold_left((state, _) => attemptMove(state, (-1, 0)), state, listRange(state.curEl.pos.x)),
    action: None
  }
  | MoveEnd => {
    ...List.fold_left((state, _) => attemptMove(state, (1, 0)), state, listRange(tileCols - state.curEl.pos.x)),
    action: None
  }
  | MoveDown => {
    ...attemptMove(state, (0, 1)),
    action: None
  }
  | CancelDown => state
  | DropDown => {
    /* Drop down until collision */
    let rec dropDown = (state) => {
      switch (attemptMoveTest(state, (0, 1))) {
      | (false, state) => state
      | (true, state) => dropDown(state)
      }
    };
    dropDown({
      ...state,
      action: None
    })
  }
  | RotateCW => {
    switch (attemptRotateCW(state)) {
    | (true, state) => {
      ...state,
      action: None
    }
    | (false, state) => {
      ...state,
      action: None
    }
    }
  }
  | RotateCCW => {
    switch (attemptRotateCCW(state)) {
    | (true, state) => {
      ...state,
      action: None
    }
    | (false, state) => {
      ...state,
      action: None
    }
    }
  }
  | Pause => {
    ...state,
    action: None,
    paused: !state.paused
  }
  | None => state
  }
};

let drawElTiles2 = (tiles, color, x, y, state) => {
    state.boardProgram.currElDraw.uniforms[0] = Gpu.Uniform.UniformVec3f(color);
    state.boardProgram.gridDraw.uniforms[2] = Gpu.Uniform.UniformVec3f(color);
    /* Translate to -1.0 to 1.0 coords */
    let tileHeight = 2.0 /. float_of_int(tileRows);
    let tileWidth = 2.0 /. float_of_int(tileCols);
    /* Translation */
    let elPos = [|
      -1. +. float_of_int(x) *. tileWidth,
      1. +. float_of_int(y) *. -.tileHeight
    |];
    state.boardProgram.currElDraw.uniforms[1] = Gpu.Uniform.UniformVec2f(elPos);
    state.boardProgram.gridDraw.uniforms[1] = Gpu.Uniform.UniformVec2f(elPos);
    state.boardProgram.currElTiles = Array.concat(List.map(((tileX, tileY)) => {
      /* 2x coord system with y 1.0 at top and -1.0 at bottom */
      let tileYScaled = float_of_int(tileY * -1) *. tileHeight;
      let tileXScaled = float_of_int(tileX) *. tileWidth;
      /* Bottom left, Top left, Top right, Bottom right */
      [|
        tileXScaled, tileYScaled -. tileHeight,
        tileXScaled, tileYScaled,
        tileXScaled +. tileWidth, tileYScaled,
        tileXScaled +. tileWidth, tileYScaled -. tileHeight
      |]
    }, tiles));
    state.boardProgram.updateCurrEl = true;
};

let afterTouchdown = (state, canvas : Gpu.Canvas.t) => {
  let curTime = state.curTime +. canvas.deltaTime;
  if (Array.length(state.lastCompletedRows) > 0) {
    /* Move rows above completed down */
    Array.iter((currentRow) => {
      for (y in currentRow downto 1) {
        state.tiles[y] = Array.copy(state.tiles[y - 1]);
        for (tileIdx in (y * tileCols) to (y * tileCols + tileCols) - 1) {
          state.boardProgram.tiles[tileIdx] = state.boardProgram.tiles[tileIdx - tileCols];
        };
      };
    }, state.lastCompletedRows);
    state.boardProgram.updateTiles = true;
  };
  let state = {
    ...state,
    curEl: newElement()
  };
  drawElTiles2(
    elTiles(state.curEl.el, state.curEl.rotation),
    tileColors2[state.curEl.color],
    state.curEl.pos.x, state.curEl.pos.y,
    state
  );
  updateBeams(state);
  if (isCollision(state)) {
    {
      ...state,
      gameState: GameOver
    }
  } else {
    {
      ...state,
      curTime: curTime,
      lastTick: curTime,
      posChanged: false,
      rotateChanged: false
    }
  }
};

let drawGame = (state, canvas : Gpu.Canvas.t) => {
  let timeStep = canvas.deltaTime;
  let curTime = state.curTime +. timeStep;
  let isNewTick = curTime > state.lastTick +. tickDuration;
  let (state, isTouchdown) = if (isNewTick) {
    switch (state.action) {
    | CancelDown => (state, false)
    | _ => {
      switch (attemptMoveTest(state, (0, 1))) {
      | (true, state) => (state, false)
      | (false, state) => (state, true)
      }
    }
    }
  } else {
    (state, false)
  };
  let state = if (state.posChanged || state.rotateChanged) {
    drawElTiles2(
      elTiles(state.curEl.el, state.curEl.rotation),
      tileColors2[state.curEl.color],
      state.curEl.pos.x, state.curEl.pos.y,
      state
    );
    updateBeams(state);
    {
      ...state,
      posChanged: false,
      rotateChanged: false
    }
  } else {
    state
  };
  /* Handle element has touched down */
  switch (isTouchdown) {
  | false =>
    {
      ...state,
      curTime: curTime,
      lastTick: (isNewTick) ? curTime : state.lastTick
    }
  | true =>
    /* Put element into tiles */
    elToTiles(state);
    /* Check for completed rows */
    let completedRows = Array.map(
      tileRow => {
        !Array.fold_left((hasEmpty, tileState) => hasEmpty || tileState == 0, false, tileRow);
      },
      state.tiles
    );
    /* Get array with indexes of completed rows */
    let (_, completedRowIndexes) = Array.fold_left(((i, rows), completed) => {
      switch (completed) {
      | true => (i + 1, Array.append(rows, [|i|]))
      | false => (i + 1, rows)
      }
    }, (0, [||]), completedRows);
    let state = {
      ...state,
      lastCompletedRows: completedRowIndexes
    };
    if (Array.length(completedRowIndexes) > 0) {
      state.boardProgram.blinkRows.rows = completedRowIndexes;
      state.boardProgram.blinkRows.state = BoardProgram.BlinkRows.Blinking;
      state
    } else {
      afterTouchdown(state, canvas)
    }
  }
};


/*
let drawInfo = (state, env) => {
  let infoOffsetX = boardOffsetX * 2 + boardWidth;
  let infoOffsetY = boardOffsetY;
  Draw.text(
    ~font=state.headingFont,
    ~body="Vimtris",
    ~pos=(infoOffsetX, infoOffsetY),
    env
  );
  List.iteri((i, text) => {
    Draw.text(
      ~font=state.infoFont,
      ~body=text,
      ~pos=(infoOffsetX + 4, infoOffsetY + 40 + (18 * i)),
      env
    );
  }, [
    "Space - pause",
    "H - move left",
    "L - move right",
    "J - move down",
    "K - cancel down",
    "W - move 3 tiles right",
    "B - move 3 tiles left",
    "0 - move leftmost",
    "$ - move rightmost",
    "S - rotate counter clockwise",
    "C - rotate clockwise",
    ". - drop",
  ]);
};
*/

let draw = (state, canvas) => {
  /* todo: Process by state, pause (etc)? */
  let state = processAction(state);
  let mainProcess = (state) => {
    let state = drawGame(state, canvas);
    BoardProgram.draw(state.boardProgram);
    switch (state.gameState) {
    | Running => state
    | GameOver => newGame(state)
    }
  };
  if (state.paused) {
    state
  } else {
    switch (state.boardProgram.blinkRows.state) {
    | BoardProgram.BlinkRows.NotBlinking =>
      mainProcess(state);
    | BoardProgram.BlinkRows.Blinking =>
      /* Blink animation */
      BoardProgram.draw(state.boardProgram);
      state
    | BoardProgram.BlinkRows.JustBlinked =>
      state.boardProgram.blinkRows.state = BoardProgram.BlinkRows.NotBlinking;
      /* Run after touchdown now that animation is done */
      let state = afterTouchdown(state, canvas);
      mainProcess(state);
    }
  }
};

let keyPressed = (state, canvas : Gpu.Canvas.t) => {
  Reasongl.Gl.Events.(
    switch (canvas.keyboard.keyCode) {
    | H => {
      ...state,
      action: MoveLeft
    }
    | L => {
      ...state,
      action: MoveRight
    }
    | W | E => {
      ...state,
      action: BlockRight
    }
    | B => {
      ...state,
      action: BlockLeft
    }
    | J => {
      ...state,
      action: MoveDown
    }
    | K => {
      ...state,
      action: CancelDown
    }
    | S | R => {
      ...state,
      action: RotateCCW
    }
    | C => {
      ...state,
      action: RotateCW
    }
    | Period => {
      ...state,
      action: DropDown
    }
    | Space => {
      ...state,
      action: Pause
    }
    | _ => {
      /* Js.log(lastKeyCode(Document.window)); */
      /* Todo: check shift press */
      switch (lastKeyCode(Document.window)) {
      | 48 | 173 => {
          ...state,
          action: MoveBeginning
        }
      | 52 => {
          ...state,
          action: MoveEnd
        }
      | _ => state
      }
    }
    }
  );
};