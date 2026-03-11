# ngtcp2 PHP拡張 開発計画

## 1. 目的とスコープ

本計画は `SPEC.md` に基づき、`ngtcp2` を Sans-I/O QUIC engine として PHP ユーザーランドへ公開する最小実装 (v0) を定義する。

v0 の実装範囲:
- client only
- GnuTLS backend only
- bidirectional stream only
- `Connection::recv() / onTimeout() / getNextTimeout() / pollEvents() / flush() / openStream() / close()`
- `Stream::read() / write() / end()`
- イベント: `HandshakeCompleted`, `ConnectionClosed`, `StreamReadable`, `StreamWritable`, `StreamClosed`, `StreamReset`

v0 で後回し:
- server mode
- QUIC DATAGRAM extension
- path migration
- 詳細統計・debug dump
- key update event の公開
- nghttp3 統合

## 2. 設計原則 (SPEC反映)

- `ngtcp2_conn` は拡張内部に閉じ込め、UDP socket と event loop はユーザーランド所有のままにする。
- native callback から PHP callback を直接呼ばず、`internal event queue` に積んで `pollEvents()` で取り出す。
- Stream payload は Event に直載せせず、内部 `rx buffer` に保持し `Stream::read()` で読む。
- `flush()` は「送信可能な QUIC packet を生成して Datagram 配列で返す」責務に限定する。
- API は分岐判断に必要な最小限のみ公開し、transport 内部情報は隠蔽する。

## 3. サンプルコードからの取り込み方針

`sample_client.c` / `sample_server.c` は「ngtcp2 + GnuTLS の接着点」を抽出して利用する。

優先して取り込む処理:
- TLS 初期化と QUIC 連携
  - `ngtcp2_crypto_gnutls_configure_client_session()`
  - `gnutls_session_set_ptr()` + `ngtcp2_crypto_conn_ref`
  - `ngtcp2_conn_set_tls_native_handle()`
- callback 登録
  - crypto callback 群 (`encrypt/decrypt/hp_mask/update_key/...`)
  - stream/handshake callback
- 入力処理
  - `ngtcp2_conn_read_pkt()`
- 出力処理
  - `ngtcp2_conn_writev_stream()`
  - `NGTCP2_ERR_WRITE_MORE` の継続処理
- timeout
  - `ngtcp2_conn_get_expiry()`
  - `ngtcp2_conn_handle_expiry()`
- close
  - `ngtcp2_conn_write_connection_close()`

拡張での変換ルール:
- サンプルの `recv/send` は削除し、`Datagram` 入出力 API に置換する。
- サンプルの `poll/libev` 制御は削除し、`getNextTimeout()/onTimeout()` API に置換する。
- サンプルで `fprintf` している callback 通知を event queue push に置換する。

## 4. 推奨ディレクトリ構成

```text
ext-ngtcp2/
  config.m4
  php_ngtcp2.h
  ngtcp2.c
  src/
    connection.c
    stream.c
    datagram.c
    address.c
    event.c
    tls_gnutls.c
    callbacks.c
    queue.c
    buffer.c
    internal/
      types.h
      connection.h
      stream.h
      event.h
      queue.h
      tls.h
      macros.h
  tests/
    001_load_extension.phpt
    010_connection_ctor.phpt
    020_datagram_recv_flush.phpt
    030_stream_read_write_end.phpt
    040_event_queue.phpt
    050_timeout.phpt
  examples/
    client_minimal.php
    client_echo_loop.php
```

## 5. 実装フェーズ

### Phase 0: ビルド土台

作業:
- `config.m4` に `ngtcp2`, `ngtcp2_crypto_gnutls`, `gnutls` の検出を追加。
- `phpize/configure/make` で空拡張がロードできる状態を作る。
- CI 用に最低限のビルドスクリプトを追加。

完了条件:
- `extension=ngtcp2` で `php -m` に表示される。

### Phase 1: Zend オブジェクト骨格

作業:
- class entry 登録: `Connection`, `Stream`, `Datagram`, `Address`, `Event` 系。
- `php_quic_connection` / `php_quic_stream` 構造体作成。
- `create_object/free_obj` と object handlers 実装。
- `Stream` は `connection参照 + stream_id` の lightweight view にする。

完了条件:
- `new Connection(...)`, `openStream()` がクラッシュなく生成できる。

### Phase 2: Connection 初期化 (client + GnuTLS)

作業:
- `tls_gnutls.c` で client session 初期化。
- `connection.c` で `ngtcp2_conn_client_new()` を実装。
- sample の callback テーブルを `callbacks.c` に分離。

完了条件:
- Connection 生成時に `ngtcp2_conn*` と `gnutls_session_t` が正しく所有され、`free_obj` で解放される。

### Phase 3: Datagram 入力 (`recv`) とイベント化

作業:
- `Connection::recv(Datagram $dgram)` を実装し `ngtcp2_conn_read_pkt()` を呼ぶ。
- callback 内で stream rx buffer を更新。
- callback 内で `StreamReadable/StreamClosed/StreamReset/HandshakeCompleted/ConnectionClosed` を queue push。

完了条件:
- 入力 datagram に応じて `pollEvents()` でイベントを取得できる。

### Phase 4: 出力 (`flush`) と送信キュー

作業:
- stream tx buffer を保持し `Stream::write()/end()` はバッファ投入のみ行う。
- `Connection::flush()` で `ngtcp2_conn_writev_stream()` をループ実行。
- 生成 packet を `Datagram[]` として返却。
- `NGTCP2_ERR_WRITE_MORE` と `wdatalen` 進行管理を実装。

完了条件:
- `write()` 後に `flush()` が 0個以上の datagram を返し、再呼び出しで drain できる。

### Phase 5: timeout API

作業:
- `getNextTimeout(): ?int` を `ngtcp2_conn_get_expiry()` ベースで実装。
- `onTimeout()` を `ngtcp2_conn_handle_expiry()` ベースで実装。
- timeout 処理でも `flush()` と event queue の整合を維持。

完了条件:
- ユーザーランド event loop から timeout 駆動で進行できる。

### Phase 6: close/draining とエラー伝播

作業:
- `Connection::close()` で `ngtcp2_conn_write_connection_close()` を使用。
- `isEstablished()/isClosed()/isDraining()` を ngtcp2 state から反映。
- ngtcp2 エラー/TLS alert を PHP 例外 or `ConnectionClosed` reason にマッピング。

完了条件:
- 正常 close/異常 close どちらもイベントと状態遷移が矛盾しない。

### Phase 7: PHPT と統合検証

作業:
- object ライフサイクル、queue、timeout、stream API の PHPT を追加。
- integration は loopback UDP + テストフィクスチャサーバで最小通信確認。

完了条件:
- v0 の public API が PHPT 一式で再現可能。

## 6. API/内部データ設計

`php_quic_connection`:
- `ngtcp2_conn *conn`
- `gnutls_session_t session`
- `gnutls_certificate_credentials_t cred`
- `HashTable streams` (`stream_id => php_quic_stream_entry`)
- `php_quic_event_queue events`
- `php_quic_dgram_queue tx_dgrams`
- state flags: `established`, `draining`, `closed`
- `local/remote Address`

`php_quic_stream_entry`:
- `int64_t stream_id`
- flags: `readable`, `writable`, `closed`, `reset`
- `rx_buffer`, `tx_buffer`
- `fin_sent`, `fin_recv`

`php_quic_event`:
- `event_type`
- `stream_id`
- `error_code`
- `by_peer`
- `reason`
- `timestamp`

## 7. テスト戦略

PHPT レイヤ:
- API 署名/戻り値/例外の検証。
- `pollEvents()` が FIFO 順で返ること。
- `StreamReadable` の payload 非同梱 (read で取得) 検証。
- `StreamClosed` と `StreamReset` の区別検証。

統合レイヤ:
- UDP ソケットを PHP 側で所有し、`recv/pollEvents/flush/onTimeout` ループで handshake 進行確認。
- 小さな payload と複数 packet payload の両方を確認。
- close/draining 遷移確認。

mock 方針:
- ngtcp2 自体は mock しない。
- queue/buffer の純粋ロジックのみ C ユニットテストまたは PHPT 補助関数で局所検証。

## 8. 失敗しやすい箇所と対策

- callback から Zend API を触る事故
  - 対策: callback は state 更新 + queue push のみ。
- `NGTCP2_ERR_WRITE_MORE` の処理漏れ
  - 対策: sample 同様に `wdatalen` を必ず反映。
- Stream object と native stream state の寿命不整合
  - 対策: stream state 実体を connection 側 HashTable へ一元化。
- timeout 駆動の欠落
  - 対策: `getNextTimeout()/onTimeout()/flush()` の順序を examples と tests で固定化。

## 9. マイルストーン

- M1: 拡張ロード + Zend class 骨格
- M2: client handshake 成立 (イベント通知まで)
- M3: 単一 bidi stream で write/read/end
- M4: timeout + close/draining 安定化
- M5: PHPT + integration 最低セット完了 (v0 リリース候補)

## 10. v0 完了定義 (DoD)

- `SPEC.md` の最小実装スコープ項目をすべて満たす。
- callback 直 PHP 呼び出しが存在しない。
- UDP socket/event loop を拡張が所有していない。
- PHPT + integration の最低スイートが CI で再現可能。
- 既知制約 (server mode 未対応等) がドキュメント化されている。

## 11. 進捗メモ (2026-03-11)

完了済み:
- Phase 0-5 相当の土台を実装済み (`recv`/`flush`/`pollEvents`/`timeout`/`close`)。
- `Varion\\Ngtcp2` 名前空間へ統一済み。
- ngtcp2 + GnuTLS 連携で client 接続オブジェクトと callback 配線を実装済み。
- `Stream::reset()` を `ngtcp2_conn_shutdown_stream()` に接続し、transport state と API state の乖離を解消。
- `close($errorCode, $reason)` で reason phrase を ngtcp2 の close error (`ngtcp2_ccerr`) に反映。
- PHPT 8件が全件 PASS。
- `gtlsserver` を使った integration handshake PHPT (`tests/100_integration_handshake.phpt`) を追加。
  UDP bind 制約環境では skip し、実行可能環境で handshake 完了を検証できる構成にした。
- handshake 後の `openStream()/write()/end()/flush()` を検証する integration PHPT
  (`tests/110_integration_stream_tx.phpt`) を追加。
- `examples/server_minimal.php` を追加し、`gtlsserver` を起動する最小サーバーラッパーを実装。
- `examples/README.md` を追加し、server/client の実行手順を明記。
- `sample_server.c` と現実装の差分整理として `docs/server_mode_gap.md` を追加。
- native server mode の実装タスク分解として `docs/server_mode_mvp_plan.md` を追加。
- QUIC DATAGRAM extension の実装前メモとして `docs/datagram_extension_plan.md` を追加。
- path migration の実装前メモとして `docs/path_migration_plan.md` を追加。
- key update event / debug stats / nghttp3 統合などの分解メモとして `docs/post_v0_backlog.md` を追加。
- `Varion\\Ngtcp2\\ServerConnection` クラス骨格を追加し、`accept()` を server mode の入口として予約。
- `ServerConnection::accept()` に Initial datagram 検証と server-side native 初期化の土台を追加
  (`certFile`/`keyFile` オプション指定時)。
- `examples/server_native_minimal.php` を追加し、`ServerConnection::accept()` を使った
  実験的な native server 受信ループ例を提供。
- `tests/120_integration_native_server_accept.phpt` を追加し、native server 受理経路で
  client/server handshake 成立を検証。
- `tests/121_integration_native_server_stream_readable.phpt` を追加し、native server 側で
  client stream 書き込みの `StreamReadable` イベントを検証。
- `Connection::getStream(int $streamId): ?Stream` を追加し、remote-open stream の取得を可能化。
- `tests/031_connection_get_stream.phpt` を追加し、既存/未知 stream ID の取得挙動を検証。
- `tests/121_integration_native_server_stream_readable.phpt` を拡張し、server 側で
  `StreamReadable` 後に payload を `Stream::read()` で取得できることを検証。
- `tests/122_integration_native_server_stream_roundtrip.phpt` を追加し、client 発ストリームに対して
  native server が返信し、client が受信できる往復経路を検証。
- `tests/021_server_close_event.phpt` を追加し、`ServerConnection::close()` の状態遷移と
  `ConnectionClosed` イベント発火を検証。
- `ServerConnection::accept()` の server transport params で
  `stateless_reset_token_present` と token 生成を明示関数化し、
  `sample_server.c` との差分を縮小。
- `tests/123_integration_native_server_close.phpt` を追加し、native server の `close()` が
  client 側の `ConnectionClosed`/`isClosed()` に伝播することを統合で検証。

未着手/残課題:
- server mode / DATAGRAM拡張 / path migration など v0 スコープ外項目の整理。

## 12. サーバースクリプト作成計画

目的:
- 現在の client-only 制約を維持したまま、実運用に近い検証を行える最小 QUIC サーバー起動スクリプトを `examples/` に追加する。
- 将来の native server mode 実装時に `sample_server.c` の接着点を再利用できるよう、移行前提の構成にする。

前提:
- 現時点の拡張 API は `Connection` が client 側初期化のみを提供しており、拡張単体で QUIC server accept loop は実装できない。
- そのため第1段階は `/usr/sbin/gtlsserver` を使ったラッパースクリプトとして提供する。

### Phase S1: 外部サーバー起動ラッパー例 (短期)

作業:
- `examples/server_minimal.php` を追加する。
  - 自己署名証明書の生成 (既存ファイルがなければ `openssl` 実行)
  - `gtlsserver` 起動 (`proc_open`) と終了処理 (`SIGTERM`, `proc_close`)
  - host/port/docroot/証明書パスを引数で指定可能にする
- `examples/README` または `tests/README.md` に実行手順を追加する。
- 既存 `client_minimal.php` から接続して handshake できることを確認する。

完了条件:
- 開発者が 2 ターミナルで `server_minimal.php` と `client_minimal.php` を実行し、接続成立を再現できる。

### Phase S2: sample_server.c 参照による native server mode 設計メモ (中期)

参照ポイント (`sample_server.c`):
- `ngtcp2_conn_server_new` 初期化経路
- server 側 transport params 設定
- server callback 群 (`recv_stream_data`, `stream_open`, `stream_close`, `handshake_completed`)
- `ngtcp2_conn_writev_stream` / `ngtcp2_conn_write_connection_close` 送信経路
- address validation, preferred address, stateless reset token

作業:
- server mode 導入時の最小 API 草案を追記する。
  - 例: `ServerConnection` または `Connection::accept(...)` 系
  - `recv/flush/pollEvents/onTimeout` の対称性維持
- client 実装と共有できる内部部品を整理する。
  - event queue / buffer / datagram 変換 / callback 登録
- `sample_server.c` と現実装の差分一覧を作成する。

完了条件:
- server mode 実装着手時に必要な差分タスクが Issue レベルで分解済み。

### Phase S3: native server mode MVP 実装 (長期)

作業:
- server-side connection 初期化を拡張へ追加する。
- Initial packet 受信から handshake 完了までのイベント遷移を実装する。
- 単一 bidi stream の受信/送信/close を client と同等 API で提供する。
- PHPT + integration (loopback) を追加する。

完了条件:
- `examples/server_native_minimal.php` と client 例で往復通信が再現できる。
- `server mode` が「v0 スコープ外」から「次期スコープ候補」へ昇格可能な品質になる。
