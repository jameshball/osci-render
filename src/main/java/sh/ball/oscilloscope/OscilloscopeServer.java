package sh.ball.oscilloscope;

import org.java_websocket.WebSocket;
import org.java_websocket.handshake.ClientHandshake;
import org.java_websocket.server.WebSocketServer;
import sh.ball.audio.AudioPlayer;

import java.net.InetSocketAddress;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.ShortBuffer;
import java.util.HashSet;
import java.util.Set;

public class OscilloscopeServer<S> extends WebSocketServer {

  private static final int TCP_PORT = 4444;
  private static final int NUM_VERTICES = 2048;
  private static final int VERTEX_SIZE = 2;

  private final Set<WebSocket> conns;
  private final AudioPlayer<S> audioPlayer;
  private final int frameSize;
  private final byte[] buffer;

  public OscilloscopeServer(AudioPlayer<S> audioPlayer, int frameSize) {
    super(new InetSocketAddress(TCP_PORT));
    this.conns = new HashSet<>();
    this.audioPlayer = audioPlayer;
    this.frameSize = frameSize;
    this.buffer = new byte[frameSize * NUM_VERTICES * VERTEX_SIZE];
  }

  public void send(byte[] bytes) {
    for (WebSocket sock : conns) {
      sock.send(bytes);
    }
  }

  @Override
  public void onOpen(WebSocket conn, ClientHandshake handshake) {
    conns.add(conn);
    System.out.println("New connection from " + conn.getRemoteSocketAddress().getAddress().getHostAddress());
  }

  @Override
  public void onClose(WebSocket conn, int code, String reason, boolean remote) {
    conns.remove(conn);
    System.out.println("Closed connection to " + conn.getRemoteSocketAddress().getAddress().getHostAddress());
  }

  @Override
  public void onMessage(WebSocket conn, String message) {
    System.out.println("Message from client: " + message);
    for (WebSocket sock : conns) {
      sock.send(message);
    }
  }

  @Override
  public void onError(WebSocket conn, Exception ex) {
    //ex.printStackTrace();
    if (conn != null) {
      conns.remove(conn);
      // do some thing if required
    }
    System.out.println("ERROR from " + conn.getRemoteSocketAddress().getAddress().getHostAddress());
  }

  @Override
  public void onStart() {
    new Thread(() -> {
      while (true) {
        try {
          audioPlayer.read(buffer);
          send(buffer);
        } catch (InterruptedException e) {
          e.printStackTrace();
        }
      }
    }).start();
  }
}