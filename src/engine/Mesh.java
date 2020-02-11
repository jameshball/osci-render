package engine;

import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.Scanner;
import java.util.regex.MatchResult;
import java.util.stream.Collectors;

public class Mesh {

  private static final String VERTEX_PATTERN = "(?m)^v .*";
  private static final String FACE_PATTERN = "(?m)^f .*";

  private List<Vector3> vertices;
  private List<Integer> edgeData;

  public Mesh() {
    this.vertices = new ArrayList<>();
    this.edgeData = new ArrayList<>();
  }

  public Mesh(List<Vector3> vertices, List<Integer> edgeData) {
    this.vertices = vertices;
    this.edgeData = edgeData;
  }

  public List<Vector3> getVertices() {
    return vertices;
  }

  public List<Integer> getEdgeData() {
    return edgeData;
  }

  public static Mesh loadFromFile(String filename) {
    Scanner sc;
    try {
      sc = new Scanner(new File(filename));
    } catch (Exception e) {
      System.err.println("Cannot load mesh data from: " + filename);
      return new Mesh();
    }
    // load vertices
    List<Vector3> vertices =
      sc.findAll(VERTEX_PATTERN)
      .map(s -> parseVertex(s.group(0)))
      .collect(Collectors.toList());
    // load edge data
    List<Integer> edgeData = new ArrayList<>();
    for(MatchResult result : sc.findAll(FACE_PATTERN).collect(Collectors.toList())) {
      List<Integer> indices = parseFace(result.group(0));
      for(int i = 0; i < indices.size(); i++ ) {
        edgeData.add(indices.get(i));
        edgeData.add(indices.get((i + 1) % indices.size()));
      }
    }
    return new Mesh(vertices, edgeData);
  }

  private static Vector3 parseVertex(String data) {
    String[] coords = data.split(" ");
    double x = Double.parseDouble(coords[1]);
    double y = Double.parseDouble(coords[2]);
    double z = Double.parseDouble(coords[3]);
    return new Vector3(x, y, z);
  }

  private static List<Integer> parseFace(String data) {
    List<Integer> indices = new ArrayList<>();
    String[] datas = data.split(" ");
    for(int i = 1; i < datas.length; i++) {
      indices.add(Integer.parseInt(datas[i].split("/")[0]) - 1);
    }
    return indices;
  }
}
