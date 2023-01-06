package sh.ball.gui;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.logging.Level;

import static sh.ball.gui.Gui.logger;

public class GitHubReleaseDetector {
  private final String username;
  private final String repo;

  public GitHubReleaseDetector(String username, String repo) {
    this.username = username;
    this.repo = repo;
  }

  public String getLatestRelease() {
    try {
      // Make the GET request and retrieve the response
      HttpURLConnection connection = (HttpURLConnection) new URL("https://api.github.com/repos/" + username + "/" + repo + "/releases").openConnection();
      connection.setRequestMethod("GET");
      BufferedReader reader = new BufferedReader(new InputStreamReader(connection.getInputStream()));
      StringBuilder response = new StringBuilder();
      String line;
      while ((line = reader.readLine()) != null) {
        response.append(line);
      }
      reader.close();

      // Parse the response as a JSON array
      JSONArray releases = new JSONArray(response.toString());

      // Find the latest release by iterating through the list of releases
      // and comparing the versions
      String latestRelease = null;
      for (int i = 0; i < releases.length(); i++) {
        JSONObject release = releases.getJSONObject(i);
        String version = release.getString("tag_name");

        // Compare versions and update the latestRelease variable if necessary
        if (compareVersions(version, latestRelease) > 0) {
          latestRelease = version;
        }
      }

      return latestRelease;
    } catch (IOException e) {
      logger.log(Level.SEVERE, "Failed to get latest release", e);
    }

    return null;
  }

  public static int compareVersions(String v1, String v2) {
    if (v1 == null) {
      return -1;
    } else if (v2 == null) {
      return 1;
    }
    v1 = v1.replaceAll("^v", "");
    v2 = v2.replaceAll("^v", "");
    // Split the version strings on "." characters
    String[] v1Parts = v1.split("\\.");
    String[] v2Parts = v2.split("\\.");

    // Compare each part of the version string
    for (int i = 0; i < Math.max(v1Parts.length, v2Parts.length); i++) {
      int v1Part = (i < v1Parts.length) ? Integer.parseInt(v1Parts[i]) : 0;
      int v2Part = (i < v2Parts.length) ? Integer.parseInt(v2Parts[i]) : 0;
      if (v1Part > v2Part) {
        return 1;
      } else if (v1Part < v2Part) {
        return -1;
      }
    }

    return 0;
  }
}
