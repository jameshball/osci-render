package sh.ball.engine;

import com.google.gson.Gson;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.*;
import java.util.stream.Collectors;

public class ObjectServer implements Runnable {

  private static final int PORT = 51677;
  private final Gson gson = new Gson();
  private final Map<String, WorldObject> objects = new HashMap<>();
  private final ObjectSet objectSet = new ObjectSet();

  @Override
  public void run() {
    try {
      ServerSocket Server = new ServerSocket(PORT);

      while (true) {
        Socket socket = Server.accept();
        BufferedReader clientReader = new BufferedReader(new InputStreamReader(socket.getInputStream()));
        String json = clientReader.readLine();
        EngineInfo info = gson.fromJson(json, EngineInfo.class);

        for (ObjectInfo obj : info.objects) {
          if (!objects.containsKey(obj.name)) {
            objects.put(obj.name, new WorldObject(obj.vertices, obj.edges, obj.faces));
          }
        }

        Set<String> currentObjects = Arrays.stream(info.objects).map(obj -> obj.name).collect(Collectors.toSet());
        objects.entrySet().removeIf(obj -> !currentObjects.contains(obj.getKey()));
        socket.close();

        objectSet.setObjects(objects.values(), Arrays.stream(info.objects).map(obj -> obj.matrix).toList(), info.focalLength);
      }
    } catch (IOException e) {
      e.printStackTrace();
    }
  }

  public ObjectSet getObjectSet() {
    return objectSet;
  }

  private static class EngineInfo {
    private ObjectInfo[] objects;
    private float focalLength;

    @Override
    public String toString() {
      return "EngineInfo{" +
        "objects=" + Arrays.toString(objects) +
        "focalLength=" + focalLength +
        '}';
    }
  }

  private static class ObjectInfo {
    private String name;
    private Vector3[] vertices;
    private int[] edges;
    private int[][] faces;
    // Camera space matrix
    private float[] matrix;

    @Override
    public String toString() {
      return "ObjectInfo{" +
        "name='" + name + '\'' +
        ", vertices=" + Arrays.toString(vertices) +
        ", edges=" + Arrays.toString(edges) +
        ", faces=" + Arrays.deepToString(faces) +
        ", matrix=" + Arrays.toString(matrix) +
        '}';
    }
  }
}
