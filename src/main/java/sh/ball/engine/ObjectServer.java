package sh.ball.engine;

import com.google.gson.Gson;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.Arrays;

public class ObjectServer implements Runnable {

  private static final int PORT = 51677;
  private final Gson gson = new Gson();

  @Override
  public void run() {
    try {
      ServerSocket Server = new ServerSocket(PORT);

      while (true) {
        System.out.println("Waiting for connection");
        Socket socket = Server.accept();
        BufferedReader clientReader = new BufferedReader(new InputStreamReader(socket.getInputStream()));
        String json = clientReader.readLine();
        System.out.println(json);
        EngineInfo info = gson.fromJson(json, EngineInfo.class);
        System.out.println(info);
        socket.close();
      }
    } catch (IOException e) {
      e.printStackTrace();
    }
  }

  private static class EngineInfo {
    private ObjectInfo[] objects;

    @Override
    public String toString() {
      return "EngineInfo{" +
        "objects=" + Arrays.toString(objects) +
        '}';
    }
  }

  private static class ObjectInfo {
    private String name;
    private Vector3[] vertices;
    private int[] edges;
    // Camera space matrix
    private float[] matrix;

    @Override
    public String toString() {
      return "ObjectInfo{" +
        "name='" + name + '\'' +
        ", vertices=" + Arrays.toString(vertices) +
        ", edges=" + Arrays.toString(edges) +
        ", matrix=" + Arrays.toString(matrix) +
        '}';
    }
  }
}
