package info.karuru.cariot.ble

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertNull
import org.junit.Test

// テストリスト（mobile/lib/ble/obd_chunk_assembler.dartの移植。責務はチャンク再構成のみに絞り、
// ObdReadingへのパース呼び出しは分離する＝呼び出し側がadd()の戻り値(結合済みバイト列)を
// ObdReading.fromBytes()に渡す設計にする）
// - [ ] 2バイト未満の入力はnullを返す（[seq][total]すら読めない不正な入力）
// - [ ] total=1のみ(seq=0)を1回addすれば、そのpayloadがそのまま結合結果として返る
// - [ ] total=2をseq=0→seq=1の順でaddすると、2回目でpayloadが連結されて返る
// - [ ] total=2で1回目(seq=0)はまだ揃っていないのでnullが返る
// - [ ] seq0が来ると常に新しいシーケンスとして状態がリセットされる（未完了状態が破棄される）
// - [ ] 揃って結合結果を返した後は内部状態がクリアされ、次のセットを新規に集められる
// - [ ] 前回と異なるtotalのseqが来たら状態をリセットして集め直す
// - [ ] reset()を呼ぶと内部状態がクリアされる
class ObdChunkAssemblerTest {
  @Test
  fun `2バイト未満の入力はnullを返す`() {
    val assembler = ObdChunkAssembler()
    assertNull(assembler.add(byteArrayOf(0x00)))
  }

  @Test
  fun `total1をseq0で1回addすればそのpayloadが結合結果として返る`() {
    val assembler = ObdChunkAssembler()
    val result = assembler.add(byteArrayOf(0x00, 0x01, 0xAA.toByte(), 0xBB.toByte()))
    assertArrayEquals(byteArrayOf(0xAA.toByte(), 0xBB.toByte()), result)
  }

  @Test
  fun `total2で1回目(seq0)はまだ揃っていないのでnullが返る`() {
    val assembler = ObdChunkAssembler()
    assertNull(assembler.add(byteArrayOf(0x00, 0x02, 0x11)))
  }

  @Test
  fun `total2をseq0からseq1の順でaddすると2回目で連結結果が返る`() {
    val assembler = ObdChunkAssembler()
    assembler.add(byteArrayOf(0x00, 0x02, 0x11))
    val result = assembler.add(byteArrayOf(0x01, 0x02, 0x22))
    assertArrayEquals(byteArrayOf(0x11, 0x22), result)
  }

  @Test
  fun `seq0が来ると常に新しいシーケンスとして状態がリセットされる`() {
    // BLE Notifyは同一Characteristicへの通知順=到着順が保証されるため「seqが逆順に届く」ことは
    // 実運用では起きない。seq=0は「新しいシーケンスの開始」の合図として、それ以前の未完了状態を
    // 無条件に破棄する（mobile/lib/ble/obd_chunk_assembler.dartと同じ仕様）。
    val assembler = ObdChunkAssembler()
    assembler.add(byteArrayOf(0x01, 0x02, 0x22)) // total=2のつもりでseq=1が届く（未完了のまま）
    val result = assembler.add(byteArrayOf(0x00, 0x01, 0x99.toByte())) // seq=0で新シーケンス開始
    assertArrayEquals(byteArrayOf(0x99.toByte()), result)
  }

  @Test
  fun `揃って結合結果を返した後は内部状態がクリアされ次のセットを新規に集められる`() {
    val assembler = ObdChunkAssembler()
    assembler.add(byteArrayOf(0x00, 0x01, 0x11))
    // 1セット目完了後、2セット目もseq=0から正しく開始できる
    val result = assembler.add(byteArrayOf(0x00, 0x01, 0x22))
    assertArrayEquals(byteArrayOf(0x22), result)
  }

  @Test
  fun `前回と異なるtotalのseqが来たら状態をリセットして集め直す`() {
    val assembler = ObdChunkAssembler()
    assembler.add(byteArrayOf(0x00, 0x02, 0x11)) // total=2で開始（1個目）
    // 次にtotal=1のseq=1が来ても、totalが食い違うのでリセットされ、seq=1だけでは揃わずnull
    assertNull(assembler.add(byteArrayOf(0x01, 0x01, 0x33)))
  }

  @Test
  fun `resetを呼ぶと内部状態がクリアされる`() {
    val assembler = ObdChunkAssembler()
    assembler.add(byteArrayOf(0x00, 0x02, 0x11)) // total=2で1個目だけ集めた状態
    assembler.reset()
    // reset後はseq=1だけ来ても（total=2のつもりで）揃わずnull
    assertNull(assembler.add(byteArrayOf(0x01, 0x02, 0x22)))
  }
}
