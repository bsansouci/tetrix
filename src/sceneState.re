type elState = {
  vo: Scene.sceneVertexObject,
  pos: Scene.sceneUniform,
  elPos: Data.Vec2.t,
  color: Scene.sceneUniform
};

type sceneLayout =
  | StartScreen
  | GameScreen
  | PauseScreen;

type sceneState = {
  tiles: array(int),
  sceneLayout,
  tilesTex: Scene.sceneTexture,
  elState,
  elCenterRadius: Scene.sceneUniform,
  nextEls: array(elState),
  holdingEl: elState,
  beamVO: Scene.sceneVertexObject,
  dropBeamVO: Scene.sceneVertexObject,
  dropColor: Scene.sceneUniform,
  blinkVO: Scene.sceneVertexObject,
  sceneLight: Light.ProgramLight.t,
  elLightPos: Scene.sceneUniform,
  sceneAndElLight: Light.ProgramLight.t,
  bgColor: Scene.sceneUniform,
  boardColor: Scene.sceneUniform,
  lineColor: Scene.sceneUniform,
  mutable gameState: Game.stateT,
  fontStore: FontStore.t
};
