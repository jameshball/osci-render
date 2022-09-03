package sh.ball.engine;

import com.google.gson.Gson;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.*;
import java.util.logging.Level;

import static sh.ball.gui.Gui.logger;

public class ObjectServer implements Runnable {

  private static final int PORT = 51677;
  private final Gson gson = new Gson();
  private final ObjectSet objectSet = new ObjectSet();
  private final Runnable enableRendering;
  private final Runnable disableRendering;

  public ObjectServer(Runnable enableRendering, Runnable disableRendering) {
    this.enableRendering = enableRendering;
    this.disableRendering = disableRendering;
  }

  @Override
  public void run() {
    try {
      // TODO: Need to make this resilient to ports already in use
      // will require using a range of ports rather than just one
      ServerSocket server = new ServerSocket(PORT);
      server.setReuseAddress(true);

      while (true) {
        Socket socket = server.accept();
        enableRendering.run();
        BufferedReader clientReader = new BufferedReader(new InputStreamReader(socket.getInputStream()));
        while (socket.isConnected()) {
          String json = clientReader.readLine();
          if (json == null || json.equals("CLOSE")) {
            socket.close();
            break;
          }
          EngineInfo info = gson.fromJson(json, EngineInfo.class);

          List<Map.Entry<Vector3[][], float[]>> orderedVertices = Arrays.stream(info.objects)
            .parallel()
            .filter(obj -> obj.vertices.length > 0)
            .map(obj -> {
              boolean[] visited = new boolean[obj.vertices.length];
              int[] order = new int[obj.vertices.length];
              visited[0] = true;
              order[0] = 0;
              Vector3 endPoint = obj.vertices[0][obj.vertices[0].length - 1];

              for (int i = 1; i < obj.vertices.length; i++) {
                int minPath = -1;
                double minDistance = Double.POSITIVE_INFINITY;
                for (int j = 0; j < obj.vertices.length; j++) {
                  if (!visited[j]) {
                    double distance = endPoint.distance(obj.vertices[j][0]);
                    if (distance < minDistance) {
                      minPath = j;
                      minDistance = distance;
                    }
                  }
                }
                visited[minPath] = true;
                order[i] = minPath;
                endPoint = obj.vertices[minPath][obj.vertices[minPath].length - 1];
              }

              Vector3[][] reorderedVertices = new Vector3[obj.vertices.length][];
              for (int i = 0; i < reorderedVertices.length; i++) {
                reorderedVertices[i] = obj.vertices[order[i]];
              }

              return Map.entry(reorderedVertices, obj.matrix);
            }).toList();

          List<Vector3[][]> vertices = orderedVertices.stream().map(Map.Entry::getKey).toList();
          List<float[]> matrices = orderedVertices.stream().map(Map.Entry::getValue).toList();

          objectSet.setObjects(vertices, matrices, info.focalLength);
        }
        disableRendering.run();
      }
    } catch (IOException e) {
      logger.log(Level.SEVERE, e.getMessage(), e);
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
    private Vector3[][] vertices;
    // Camera space matrix
    private float[] matrix;
  }
}
