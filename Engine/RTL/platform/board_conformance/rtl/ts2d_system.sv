// Integrated vendor-neutral CPU/GPU/display subsystem. All AXI in clk domain.
`timescale 1ns/1ps
module ts2d_system(
input wire clk,rst,pixel_clk,pixel_rst,
input wire [15:0] paddr,
input wire psel,penable,pwrite,
input wire [31:0] pwdata,
output wire [31:0] prdata,
output wire pready,pslverror,irq,
output wire hs,vs,de,
output wire [7:0] red,green,blue,
output wire [23:0] audio_l,audio_r,
output wire audio_valid,
input wire [31:0] cpu_awaddr,
input wire [7:0] cpu_awid,
input wire [7:0] cpu_awlen,
input wire [2:0] cpu_awsize,
input wire [1:0] cpu_awburst,
input wire [0:0] cpu_awvalid,
input wire [127:0] cpu_wdata,
input wire [15:0] cpu_wstrb,
input wire [0:0] cpu_wlast,
input wire [0:0] cpu_wvalid,
input wire [0:0] cpu_bready,
input wire [31:0] cpu_araddr,
input wire [7:0] cpu_arid,
input wire [7:0] cpu_arlen,
input wire [2:0] cpu_arsize,
input wire [1:0] cpu_arburst,
input wire [0:0] cpu_arvalid,
input wire [0:0] cpu_rready,
output wire [0:0] cpu_awready,
output wire [0:0] cpu_wready,
output wire [1:0] cpu_bresp,
output wire [7:0] cpu_bid,
output wire [0:0] cpu_bvalid,
output wire [0:0] cpu_arready,
output wire [127:0] cpu_rdata,
output wire [7:0] cpu_rid,
output wire [1:0] cpu_rresp,
output wire [0:0] cpu_rlast,
output wire [0:0] cpu_rvalid,
output wire [31:0] mem_awaddr,
output wire [7:0] mem_awid,
output wire [7:0] mem_awlen,
output wire [2:0] mem_awsize,
output wire [1:0] mem_awburst,
output wire [0:0] mem_awvalid,
output wire [127:0] mem_wdata,
output wire [15:0] mem_wstrb,
output wire [0:0] mem_wlast,
output wire [0:0] mem_wvalid,
output wire [0:0] mem_bready,
output wire [31:0] mem_araddr,
output wire [7:0] mem_arid,
output wire [7:0] mem_arlen,
output wire [2:0] mem_arsize,
output wire [1:0] mem_arburst,
output wire [0:0] mem_arvalid,
output wire [0:0] mem_rready,
input wire [0:0] mem_awready,
input wire [0:0] mem_wready,
input wire [1:0] mem_bresp,
input wire [7:0] mem_bid,
input wire [0:0] mem_bvalid,
input wire [0:0] mem_arready,
input wire [127:0] mem_rdata,
input wire [7:0] mem_rid,
input wire [1:0] mem_rresp,
input wire [0:0] mem_rlast,
input wire [0:0] mem_rvalid);
wire [95:0] bus_awaddr;
wire [31:0] gpu_awaddr,scan_awaddr;
wire [23:0] bus_awid;
wire [7:0] gpu_awid,scan_awid;
wire [23:0] bus_awlen;
wire [7:0] gpu_awlen,scan_awlen;
wire [8:0] bus_awsize;
wire [2:0] gpu_awsize,scan_awsize;
wire [5:0] bus_awburst;
wire [1:0] gpu_awburst,scan_awburst;
wire [2:0] bus_awvalid;
wire [0:0] gpu_awvalid,scan_awvalid;
wire [383:0] bus_wdata;
wire [127:0] gpu_wdata,scan_wdata;
wire [47:0] bus_wstrb;
wire [15:0] gpu_wstrb,scan_wstrb;
wire [2:0] bus_wlast;
wire [0:0] gpu_wlast,scan_wlast;
wire [2:0] bus_wvalid;
wire [0:0] gpu_wvalid,scan_wvalid;
wire [2:0] bus_bready;
wire [0:0] gpu_bready,scan_bready;
wire [95:0] bus_araddr;
wire [31:0] gpu_araddr,scan_araddr;
wire [23:0] bus_arid;
wire [7:0] gpu_arid,scan_arid;
wire [23:0] bus_arlen;
wire [7:0] gpu_arlen,scan_arlen;
wire [8:0] bus_arsize;
wire [2:0] gpu_arsize,scan_arsize;
wire [5:0] bus_arburst;
wire [1:0] gpu_arburst,scan_arburst;
wire [2:0] bus_arvalid;
wire [0:0] gpu_arvalid,scan_arvalid;
wire [2:0] bus_rready;
wire [0:0] gpu_rready,scan_rready;
wire [2:0] bus_awready;
wire [0:0] gpu_awready,scan_awready;
wire [2:0] bus_wready;
wire [0:0] gpu_wready,scan_wready;
wire [5:0] bus_bresp;
wire [1:0] gpu_bresp,scan_bresp;
wire [23:0] bus_bid;
wire [7:0] gpu_bid,scan_bid;
wire [2:0] bus_bvalid;
wire [0:0] gpu_bvalid,scan_bvalid;
wire [2:0] bus_arready;
wire [0:0] gpu_arready,scan_arready;
wire [383:0] bus_rdata;
wire [127:0] gpu_rdata,scan_rdata;
wire [23:0] bus_rid;
wire [7:0] gpu_rid,scan_rid;
wire [5:0] bus_rresp;
wire [1:0] gpu_rresp,scan_rresp;
wire [2:0] bus_rlast;
wire [0:0] gpu_rlast,scan_rlast;
wire [2:0] bus_rvalid;
wire [0:0] gpu_rvalid,scan_rvalid;
assign bus_awaddr={scan_awaddr,gpu_awaddr,cpu_awaddr};
assign bus_awid={scan_awid,gpu_awid,cpu_awid};
assign bus_awlen={scan_awlen,gpu_awlen,cpu_awlen};
assign bus_awsize={scan_awsize,gpu_awsize,cpu_awsize};
assign bus_awburst={scan_awburst,gpu_awburst,cpu_awburst};
assign bus_awvalid={scan_awvalid,gpu_awvalid,cpu_awvalid};
assign bus_wdata={scan_wdata,gpu_wdata,cpu_wdata};
assign bus_wstrb={scan_wstrb,gpu_wstrb,cpu_wstrb};
assign bus_wlast={scan_wlast,gpu_wlast,cpu_wlast};
assign bus_wvalid={scan_wvalid,gpu_wvalid,cpu_wvalid};
assign bus_bready={scan_bready,gpu_bready,cpu_bready};
assign bus_araddr={scan_araddr,gpu_araddr,cpu_araddr};
assign bus_arid={scan_arid,gpu_arid,cpu_arid};
assign bus_arlen={scan_arlen,gpu_arlen,cpu_arlen};
assign bus_arsize={scan_arsize,gpu_arsize,cpu_arsize};
assign bus_arburst={scan_arburst,gpu_arburst,cpu_arburst};
assign bus_arvalid={scan_arvalid,gpu_arvalid,cpu_arvalid};
assign bus_rready={scan_rready,gpu_rready,cpu_rready};
assign {scan_awready,gpu_awready,cpu_awready}=bus_awready;
assign {scan_wready,gpu_wready,cpu_wready}=bus_wready;
assign {scan_bresp,gpu_bresp,cpu_bresp}=bus_bresp;
assign {scan_bid,gpu_bid,cpu_bid}=bus_bid;
assign {scan_bvalid,gpu_bvalid,cpu_bvalid}=bus_bvalid;
assign {scan_arready,gpu_arready,cpu_arready}=bus_arready;
assign {scan_rdata,gpu_rdata,cpu_rdata}=bus_rdata;
assign {scan_rid,gpu_rid,cpu_rid}=bus_rid;
assign {scan_rresp,gpu_rresp,cpu_rresp}=bus_rresp;
assign {scan_rlast,gpu_rlast,cpu_rlast}=bus_rlast;
assign {scan_rvalid,gpu_rvalid,cpu_rvalid}=bus_rvalid;
ts2d_axi_arbiter arb(.clk(clk),.rst(rst),
.s_awaddr(bus_awaddr),.m_awaddr(mem_awaddr),
.s_awid(bus_awid),.m_awid(mem_awid),
.s_awlen(bus_awlen),.m_awlen(mem_awlen),
.s_awsize(bus_awsize),.m_awsize(mem_awsize),
.s_awburst(bus_awburst),.m_awburst(mem_awburst),
.s_awvalid(bus_awvalid),.m_awvalid(mem_awvalid),
.s_wdata(bus_wdata),.m_wdata(mem_wdata),
.s_wstrb(bus_wstrb),.m_wstrb(mem_wstrb),
.s_wlast(bus_wlast),.m_wlast(mem_wlast),
.s_wvalid(bus_wvalid),.m_wvalid(mem_wvalid),
.s_bready(bus_bready),.m_bready(mem_bready),
.s_araddr(bus_araddr),.m_araddr(mem_araddr),
.s_arid(bus_arid),.m_arid(mem_arid),
.s_arlen(bus_arlen),.m_arlen(mem_arlen),
.s_arsize(bus_arsize),.m_arsize(mem_arsize),
.s_arburst(bus_arburst),.m_arburst(mem_arburst),
.s_arvalid(bus_arvalid),.m_arvalid(mem_arvalid),
.s_rready(bus_rready),.m_rready(mem_rready),
.s_awready(bus_awready),.m_awready(mem_awready),
.s_wready(bus_wready),.m_wready(mem_wready),
.s_bresp(bus_bresp),.m_bresp(mem_bresp),
.s_bid(bus_bid),.m_bid(mem_bid),
.s_bvalid(bus_bvalid),.m_bvalid(mem_bvalid),
.s_arready(bus_arready),.m_arready(mem_arready),
.s_rdata(bus_rdata),.m_rdata(mem_rdata),
.s_rid(bus_rid),.m_rid(mem_rid),
.s_rresp(bus_rresp),.m_rresp(mem_rresp),
.s_rlast(bus_rlast),.m_rlast(mem_rlast),
.s_rvalid(bus_rvalid),.m_rvalid(mem_rvalid));
wire [31:0] csr_data,display_data;
wire gpu_busy,gpu_done,gpu_error,cmd_we,start,mode;
wire [9:0] cmd_addr;wire [255:0] cmd_data;wire [10:0] cmd_count;
wire [15:0] width,height,stride;wire [31:0] fb;
wire csr_ready,csr_error;
wire [31:0] perf_0;
wire [31:0] perf_1;
wire [31:0] perf_2;
wire [31:0] perf_3;
wire [31:0] perf_4;
wire [31:0] perf_5;
wire [31:0] perf_6;
wire [31:0] perf_7;
wire [31:0] perf_8;
wire [31:0] perf_9;
wire [31:0] perf_10;
wire [31:0] perf_11;
wire [31:0] perf_12;
wire [31:0] perf_13;
reg [31:0] perf_snapshot[0:13];
always @(posedge clk)begin
 if(rst)perf_snapshot[0]<=0;else if(gpu_done)perf_snapshot[0]<=perf_0;
 if(rst)perf_snapshot[1]<=0;else if(gpu_done)perf_snapshot[1]<=perf_1;
 if(rst)perf_snapshot[2]<=0;else if(gpu_done)perf_snapshot[2]<=perf_2;
 if(rst)perf_snapshot[3]<=0;else if(gpu_done)perf_snapshot[3]<=perf_3;
 if(rst)perf_snapshot[4]<=0;else if(gpu_done)perf_snapshot[4]<=perf_4;
 if(rst)perf_snapshot[5]<=0;else if(gpu_done)perf_snapshot[5]<=perf_5;
 if(rst)perf_snapshot[6]<=0;else if(gpu_done)perf_snapshot[6]<=perf_6;
 if(rst)perf_snapshot[7]<=0;else if(gpu_done)perf_snapshot[7]<=perf_7;
 if(rst)perf_snapshot[8]<=0;else if(gpu_done)perf_snapshot[8]<=perf_8;
 if(rst)perf_snapshot[9]<=0;else if(gpu_done)perf_snapshot[9]<=perf_9;
 if(rst)perf_snapshot[10]<=0;else if(gpu_done)perf_snapshot[10]<=perf_10;
 if(rst)perf_snapshot[11]<=0;else if(gpu_done)perf_snapshot[11]<=perf_11;
 if(rst)perf_snapshot[12]<=0;else if(gpu_done)perf_snapshot[12]<=perf_12;
 if(rst)perf_snapshot[13]<=0;else if(gpu_done)perf_snapshot[13]<=perf_13;
end
wire perf_select=paddr>=16'h60 && paddr<16'h98;
wire osd_select=paddr==16'h700 || (paddr>=16'h800 && paddr<16'hc00);
wire [31:0] osd_data;

wire audio_select=paddr>=16'h200 && paddr<16'h240;
wire [31:0] audio_data;
ts2d_audio_apb #(.PIXEL_HZ(148750000)) audio(.clk(clk),.rst(rst),
 .pixel_clk(pixel_clk),.pixel_rst(pixel_rst),.paddr(paddr),.psel(psel && audio_select),
 .penable(penable),.pwrite(pwrite),.pwdata(pwdata),.prdata(audio_data),
 .audio_l(audio_l),.audio_r(audio_r),.audio_valid(audio_valid));
wire display_select=paddr>=16'h400 && paddr<16'h420;
assign prdata=audio_select ? audio_data : osd_select ? osd_data : perf_select ? perf_snapshot[(paddr-16'h60)>>2] : display_select ? display_data : csr_data;
assign pready=(audio_select || display_select)?1'b1:csr_ready;
assign pslverror=(audio_select || display_select)?1'b0:csr_error;
ts2d_csr_apb csr(.clk_i(clk),.rst_i(rst),.apb_paddr_i(paddr),
 .apb_psel_i(psel && !display_select && !perf_select && !osd_select && !audio_select),.apb_penable_i(penable),.apb_pwrite_i(pwrite),
 .apb_pwdata_i(pwdata),.apb_prdata_o(csr_data),.apb_pready_o(csr_ready),.apb_pslverror_o(csr_error),
 .irq_pending_o(irq),.renderer_busy_i(gpu_busy),.renderer_done_i(gpu_done),.renderer_error_i(gpu_error),
 .renderer_cmd_we_o(cmd_we),.renderer_cmd_addr_o(cmd_addr),.renderer_cmd_data_o(cmd_data),
 .renderer_start_o(start),.renderer_tile_mode_o(mode),.renderer_width_o(width),.renderer_height_o(height),
 .renderer_framebuffer_base_o(fb),.renderer_framebuffer_stride_o(stride),.renderer_command_count_o(cmd_count));
assign gpu_awid=0;assign gpu_arid=0;
frame_renderer_pb3 #(.NARROW_DIRECT_PIXELS(32)) renderer(.clk_i(clk),.rst_n_i(!rst),
 .cmd_we_i(cmd_we),.cmd_addr_i(cmd_addr),.cmd_wdata_i(cmd_data),.start_i(start),.tile_mode_i(mode),
 .frame_width_i(width),.frame_height_i(height),.framebuffer_base_i(fb),.framebuffer_stride_i(stride),
 .perf_pixels_o(perf_0),.axi_bus_read_bytes_o(perf_1),.axi_bus_write_bytes_o(perf_2),.read_stall_cycles_o(perf_3),.write_stall_cycles_o(perf_4),.axi_read_bursts_o(perf_5),.axi_write_bursts_o(perf_6),.axi_read_beats_o(perf_7),.axi_write_beats_o(perf_8),.source_cache_hits_o(perf_9),.source_cache_misses_o(perf_10),.source_cache_refill_bytes_o(perf_11),.axi_useful_read_bytes_o(perf_12),.axi_useful_write_bytes_o(perf_13),
 .command_count_i(cmd_count),.busy_o(gpu_busy),.done_o(gpu_done),.error_o(gpu_error),
.m_axi_awaddr(gpu_awaddr),
.m_axi_awlen(gpu_awlen),
.m_axi_awsize(gpu_awsize),
.m_axi_awburst(gpu_awburst),
.m_axi_awvalid(gpu_awvalid),
.m_axi_wdata(gpu_wdata),
.m_axi_wstrb(gpu_wstrb),
.m_axi_wlast(gpu_wlast),
.m_axi_wvalid(gpu_wvalid),
.m_axi_bready(gpu_bready),
.m_axi_araddr(gpu_araddr),
.m_axi_arlen(gpu_arlen),
.m_axi_arsize(gpu_arsize),
.m_axi_arburst(gpu_arburst),
.m_axi_arvalid(gpu_arvalid),
.m_axi_rready(gpu_rready),
.m_axi_awready(gpu_awready),
.m_axi_wready(gpu_wready),
.m_axi_bresp(gpu_bresp),
.m_axi_bvalid(gpu_bvalid),
.m_axi_arready(gpu_arready),
.m_axi_rdata(gpu_rdata),
.m_axi_rresp(gpu_rresp),
.m_axi_rlast(gpu_rlast),
.m_axi_rvalid(gpu_rvalid));
assign scan_awaddr=0;
assign scan_awid=0;
assign scan_awlen=0;
assign scan_awsize=0;
assign scan_awburst=0;
assign scan_awvalid=0;
assign scan_wdata=0;
assign scan_wstrb=0;
assign scan_wlast=0;
assign scan_wvalid=0;
assign scan_bready=0;
assign scan_arid=0;
wire [11:0] px;wire [10:0] py;wire osd_lit,osd_area;
ts2d_osd osd(.clk(clk),.rst(rst),.pixel_clk(pixel_clk),.pixel_rst(pixel_rst),
 .paddr(paddr),.psel(psel && osd_select),.penable(penable),.pwrite(pwrite),.pwdata(pwdata),.prdata(osd_data),
 .h(px),.v(py),.lit(osd_lit),.area(osd_area));
ts2d_scanout display(.pixel_x(px),.pixel_y(py),.osd_lit(osd_lit),.osd_area(osd_area),.clk(clk),.rst(rst),.pixel_clk(pixel_clk),.pixel_rst(pixel_rst),
 .paddr(paddr),.psel(psel && display_select),.penable(penable),.pwrite(pwrite),.pwdata(pwdata),
 .prdata(display_data),.hs(hs),.vs(vs),.de(de),.red(red),.green(green),.blue(blue),
.araddr(scan_araddr),
.arlen(scan_arlen),
.arsize(scan_arsize),
.arburst(scan_arburst),
.arvalid(scan_arvalid),
.arready(scan_arready),
.rdata(scan_rdata),
.rresp(scan_rresp),
.rlast(scan_rlast),
.rvalid(scan_rvalid),
.rready(scan_rready));
endmodule
