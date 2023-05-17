// Copyright (c) 2020 ETH Zurich and University of Bologna
// Copyright and related rights are licensed under the Solderpad Hardware
// License, Version 0.51 (the "License"); you may not use this file except in
// compliance with the License.  You may obtain a copy of the License at
// http://solderpad.org/licenses/SHL-0.51. Unless required by applicable law
// or agreed to in writing, software, hardware and materials distributed under
// this License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

module apb_stdout #(
  parameter int unsigned  N_CORES     = 0,
  parameter int unsigned  N_CLUSTERS  = 0,
  parameter int unsigned  ADDR_WIDTH  = 0,
  parameter int unsigned  DATA_WIDTH  = 0
) (
  input  logic  clk_i,
  input  logic  rst_ni,
  APB_BUS.Slave apb
);
  `ifndef TARGET_SYNTHESIS
  byte buffer [N_CLUSTERS-1:0][N_CORES-1:0][$];

  function void flush(int unsigned i_cl, int unsigned i_core);
    automatic string s;
    for (int i_char = 0; i_char < buffer[i_cl][i_core].size(); i_char++) begin
      s = $sformatf("%s%c", s, buffer[i_cl][i_core][i_char]);
    end
    if (s.len() > 0) begin
      $display("[%01d,%01d] %s", i_cl, i_core, s);
    end
    buffer[i_cl][i_core] = {};
  endfunction

  function void append(int unsigned i_cl, int unsigned i_core, byte ch);
    if (ch == 8'hA) begin
      flush(i_cl, i_core);
    end else begin
      buffer[i_cl][i_core].push_back(ch);
    end
  endfunction

  always_ff @(posedge clk_i or negedge rst_ni) begin
    logic [7:0] cl_idx, core_idx;
    logic [7:0] data;
    if (!rst_ni) begin
      for (int i_cl = 0; i_cl < N_CLUSTERS; i_cl++) begin
        for (int i_core = 0; i_core < N_CORES; i_core++) begin
          flush(i_cl, i_core);
        end
      end
    end else begin
      if (apb.psel && apb.penable && apb.pwrite) begin
        cl_idx = (apb.paddr >> 7) & 32'hF;
        core_idx = (apb.paddr >> 3) & 32'hF;
        if (cl_idx < N_CLUSTERS && core_idx < N_CORES) begin
          data = apb.pwdata & 32'hFF;
          append(cl_idx, core_idx, data);
        end
      end
    end
  end

  assign apb.pready = 1'b1;

  `else

  logic data_valid, wr_ack;
  logic [31:0] din, dout;
  logic empty, full, overflow, underflow;
  logic rd_en, wr_en, rd_rst_busy, wr_rst_busy;
  // xpm_fifo_sync: Synchronous FIFO
  // Xilinx Parameterized Macro, version 2020.2

  logic [7:0] cl_idx, core_idx;
  logic [7:0] data;

  assign cl_idx = (apb.paddr >> 7) & 32'hF;
  assign core_idx = (apb.paddr >> 3) & 32'hF;
  assign data = apb.pwdata & 32'hFF;

  xpm_fifo_sync #(
    .DOUT_RESET_VALUE("0"),    // String
    .ECC_MODE("no_ecc"),       // String
    .FIFO_MEMORY_TYPE("auto"), // String
    .FIFO_READ_LATENCY(1),     // DECIMAL
    .FIFO_WRITE_DEPTH(8192),    // DECIMAL
    .FULL_RESET_VALUE(0),      // DECIMAL
    .PROG_EMPTY_THRESH(10),    // DECIMAL
    .PROG_FULL_THRESH(10),     // DECIMAL
    .RD_DATA_COUNT_WIDTH(1),   // DECIMAL
    .READ_DATA_WIDTH(32),      // DECIMAL
    .READ_MODE("fwft"),        // String
    .SIM_ASSERT_CHK(0),        // DECIMAL; 0=disable simulation messages, 1=enable simulation messages
    .USE_ADV_FEATURES("1707"), // String - enable data_valid
    .WAKEUP_TIME(0),           // DECIMAL
    .WRITE_DATA_WIDTH(32),     // DECIMAL
    .WR_DATA_COUNT_WIDTH(1)    // DECIMAL
  )
  xpm_fifo_sync_inst (
    .almost_empty(),   // 1-bit output: Almost Empty : When asserted, this signal indicates that
                                    // only one more read can be performed before the FIFO goes to empty.

    .almost_full(),     // 1-bit output: Almost Full: When asserted, this signal indicates that
                                    // only one more write can be performed before the FIFO is full.

    .data_valid(data_valid),       // 1-bit output: Read Data Valid: When asserted, this signal indicates
                                    // that valid data is available on the output bus (dout).

    .dbiterr(),             // 1-bit output: Double Bit Error: Indicates that the ECC decoder detected
                                    // a double-bit error and data in the FIFO core is corrupted.

    .dout(dout),                   // READ_DATA_WIDTH-bit output: Read Data: The output data bus is driven
                                    // when reading the FIFO.

    .empty(empty),                 // 1-bit output: Empty Flag: When asserted, this signal indicates that the
                                    // FIFO is empty. Read requests are ignored when the FIFO is empty,
                                    // initiating a read while empty is not destructive to the FIFO.

    .full(full),                   // 1-bit output: Full Flag: When asserted, this signal indicates that the
                                    // FIFO is full. Write requests are ignored when the FIFO is full,
                                    // initiating a write when the FIFO is full is not destructive to the
                                    // contents of the FIFO.

    .overflow(overflow),           // 1-bit output: Overflow: This signal indicates that a write request
                                    // (wren) during the prior clock cycle was rejected, because the FIFO is
                                    // full. Overflowing the FIFO is not destructive to the contents of the
                                    // FIFO.

    .prog_empty(),       // 1-bit output: Programmable Empty: This signal is asserted when the
                                    // number of words in the FIFO is less than or equal to the programmable
                                    // empty threshold value. It is de-asserted when the number of words in
                                    // the FIFO exceeds the programmable empty threshold value.

    .prog_full(),         // 1-bit output: Programmable Full: This signal is asserted when the
                                    // number of words in the FIFO is greater than or equal to the
                                    // programmable full threshold value. It is de-asserted when the number of
                                    // words in the FIFO is less than the programmable full threshold value.

    .rd_data_count(), // RD_DATA_COUNT_WIDTH-bit output: Read Data Count: This bus indicates the
                                    // number of words read from the FIFO.

    .rd_rst_busy(rd_rst_busy),     // 1-bit output: Read Reset Busy: Active-High indicator that the FIFO read
                                    // domain is currently in a reset state.

    .sbiterr(),             // 1-bit output: Single Bit Error: Indicates that the ECC decoder detected
                                    // and fixed a single-bit error.

    .underflow(underflow),         // 1-bit output: Underflow: Indicates that the read request (rd_en) during
                                    // the previous clock cycle was rejected because the FIFO is empty. Under
                                    // flowing the FIFO is not destructive to the FIFO.

    .wr_ack(wr_ack),               // 1-bit output: Write Acknowledge: This signal indicates that a write
                                    // request (wr_en) during the prior clock cycle is succeeded.

    .wr_data_count(), // WR_DATA_COUNT_WIDTH-bit output: Write Data Count: This bus indicates
                                    // the number of words written into the FIFO.

    .wr_rst_busy(wr_rst_busy),     // 1-bit output: Write Reset Busy: Active-High indicator that the FIFO
                                    // write domain is currently in a reset state.

    .din(din),                     // WRITE_DATA_WIDTH-bit input: Write Data: The input data bus used when
                                    // writing the FIFO.

    .injectdbiterr(), // 1-bit input: Double Bit Error Injection: Injects a double bit error if
                                    // the ECC feature is used on block RAMs or UltraRAM macros.

    .injectsbiterr(), // 1-bit input: Single Bit Error Injection: Injects a single bit error if
                                    // the ECC feature is used on block RAMs or UltraRAM macros.

    .rd_en(rd_en),                 // 1-bit input: Read Enable: If the FIFO is not empty, asserting this
                                    // signal causes data (on dout) to be read from the FIFO. Must be held
                                    // active-low when rd_rst_busy is active high.

    .rst(!rst_ni),                  // 1-bit input: Reset: Must be synchronous to wr_clk. The clock(s) can be
                                    // unstable at the time of applying reset, but reset must be released only
                                    // after the clock(s) is/are stable.

    .sleep('b0),                 // 1-bit input: Dynamic power saving- If sleep is High, the memory/fifo
                                    // block is in power saving mode.

    .wr_clk(clk_i),               // 1-bit input: Write clock: Used for write operation. wr_clk must be a
                                    // free running clock.

    .wr_en(wr_en)                  // 1-bit input: Write Enable: If the FIFO is not full, asserting this
                                    // signal causes data (on din) to be written to the FIFO Must be held
                                    // active-low when rst or wr_rst_busy or rd_rst_busy is active high

  );

  always_ff @(posedge clk_i or negedge rst_ni) begin
    apb.pready <= !full;
    
    if (apb.psel && apb.penable && apb.pready && apb.pwrite) begin
      if (cl_idx < N_CLUSTERS && core_idx < N_CORES) begin
        if (!wr_rst_busy) begin
          din <= {8'b0, cl_idx, core_idx, data};
          wr_en <= 'b1;
          apb.pready <= 1'b0;
        end
      end
    end else begin
      wr_en <= 'b0;
    end

    if (wr_ack) begin
      apb.pready <= 1'b1;
    end

    if (!rst_ni) begin
      din <= 32'h0;
      wr_en <= 1'b0;
      apb.pready <= 1'b0;
    end
  end

  // End of xpm_fifo_sync_inst instantiation
  `endif

  assign apb.prdata = '0;
  assign apb.pslverr = 1'b0;


endmodule
