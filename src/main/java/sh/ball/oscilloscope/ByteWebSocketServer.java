package sh.ball.oscilloscope;

import org.java_websocket.WebSocket;
import org.java_websocket.handshake.ClientHandshake;
import org.java_websocket.server.WebSocketServer;

import java.net.InetSocketAddress;
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.logging.Level;

import static sh.ball.gui.Gui.logger;

public class ByteWebSocketServer extends WebSocketServer {

  private static final int TCP_PORT = 42988;

  private final Set<WebSocket> conns;

  public ByteWebSocketServer() {
    super(new InetSocketAddress(TCP_PORT));
    this.conns = ConcurrentHashMap.newKeySet();
  }

  public synchronized void send(byte[] bytes) {
    for (WebSocket sock : conns) {
      try {
        sock.send(bytes);
      } catch (Exception e) {
        conns.remove(sock);
        logger.log(Level.SEVERE, e.getMessage(), e);
      }
    }
  }

  @Override
  public synchronized void onOpen(WebSocket conn, ClientHandshake handshake) {
    conns.add(conn);
  }

  @Override
  public synchronized void onClose(WebSocket conn, int code, String reason, boolean remote) {
    conns.remove(conn);
  }

  @Override
  public synchronized void onMessage(WebSocket conn, String message) {}

  @Override
  public synchronized void onError(WebSocket conn, Exception ex) {
    ex.printStackTrace();
    if (conn != null) {
      conns.remove(conn);
    }
  }

  public synchronized void shutdown() {
    conns.forEach(WebSocket::close);
    conns.clear();
  }

  @Override
  public void onStart() {}
}