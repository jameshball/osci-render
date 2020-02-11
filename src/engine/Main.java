package engine;

public class Main {
  public static void main(String[] args) throws InterruptedException {
    Camera camera = new Camera();

    WorldObject cube = new WorldObject("resources/machine.obj", new Vector3(0,0,0), new Vector3());
    while(true) {
      camera.draw(cube);
      cube.rotate(new Vector3(
        0,
        Math.PI / 100,
        0
      ));
      Thread.sleep(1000/30);
    }
  }
}
