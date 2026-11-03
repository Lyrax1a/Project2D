// Three AXI128 masters. Independent round-robin read/write arbitration.
// Writes retain one owner through B. Reads retain full owner/ID descriptors
// in an ordered queue; downstream ID zero guarantees in-order responses.
`timescale 1ns/1ps
module ts2d_axi_arbiter #(
parameter integer READ_OUTSTANDING=4
)(
input wire clk, rst,
input wire [95:0] s_awaddr,
input wire [23:0] s_awid,
input wire [23:0] s_awlen,
input wire [8:0] s_awsize,
input wire [5:0] s_awburst,
input wire [2:0] s_awvalid,
input wire [383:0] s_wdata,
input wire [47:0] s_wstrb,
input wire [2:0] s_wlast,
input wire [2:0] s_wvalid,
input wire [2:0] s_bready,
input wire [95:0] s_araddr,
input wire [23:0] s_arid,
input wire [23:0] s_arlen,
input wire [8:0] s_arsize,
input wire [5:0] s_arburst,
input wire [2:0] s_arvalid,
input wire [2:0] s_rready,
output reg [2:0] s_awready,
output reg [2:0] s_wready,
output reg [5:0] s_bresp,
output reg [23:0] s_bid,
output reg [2:0] s_bvalid,
output reg [2:0] s_arready,
output reg [383:0] s_rdata,
output reg [23:0] s_rid,
output reg [5:0] s_rresp,
output reg [2:0] s_rlast,
output reg [2:0] s_rvalid,
output reg [31:0] m_awaddr,
output reg [7:0] m_awid,
output reg [7:0] m_awlen,
output reg [2:0] m_awsize,
output reg [1:0] m_awburst,
output reg [0:0] m_awvalid,
output reg [127:0] m_wdata,
output reg [15:0] m_wstrb,
output reg [0:0] m_wlast,
output reg [0:0] m_wvalid,
output reg [0:0] m_bready,
output reg [31:0] m_araddr,
output reg [7:0] m_arid,
output reg [7:0] m_arlen,
output reg [2:0] m_arsize,
output reg [1:0] m_arburst,
output reg [0:0] m_arvalid,
output reg [0:0] m_rready,
input wire [0:0] m_awready,
input wire [0:0] m_wready,
input wire [1:0] m_bresp,
input wire [7:0] m_bid,
input wire [0:0] m_bvalid,
input wire [0:0] m_arready,
input wire [127:0] m_rdata,
input wire [7:0] m_rid,
input wire [1:0] m_rresp,
input wire [0:0] m_rlast,
input wire [0:0] m_rvalid);
reg [1:0] ws,rs,wo,ro,wnext,rnext;
reg [7:0] wid;
localparam integer READ_PTR_WIDTH=READ_OUTSTANDING<2?1:$clog2(READ_OUTSTANDING);
reg [1:0] read_owner[0:READ_OUTSTANDING-1];
reg [7:0] read_id[0:READ_OUTSTANDING-1];
reg [READ_PTR_WIDTH-1:0] read_head,read_tail;
reg [READ_PTR_WIDTH:0] read_count;
wire [1:0] response_owner=read_owner[read_head];
wire read_push=m_arvalid && m_arready;
wire read_pop=read_count!=0 && m_rvalid && m_rready && m_rlast;
integer wi,ri,wcandidate,rcandidate;
reg wfound,rfound;
always @* begin
 wcandidate=0;rcandidate=0;wfound=0;rfound=0;
 for(wi=0;wi<3;wi=wi+1)begin
  if(!wfound && s_awvalid[(wnext+wi)%3])begin wcandidate=(wnext+wi)%3;wfound=1;end
 end
 for(ri=0;ri<3;ri=ri+1)begin
  if(!rfound && s_arvalid[(rnext+ri)%3])begin rcandidate=(rnext+ri)%3;rfound=1;end
 end
 s_awready=0;
 s_wready=0;
 s_bresp=0;
 s_bid=0;
 s_bvalid=0;
 s_arready=0;
 s_rdata=0;
 s_rid=0;
 s_rresp=0;
 s_rlast=0;
 s_rvalid=0;
 m_awaddr=0;
 m_awid=0;
 m_awlen=0;
 m_awsize=0;
 m_awburst=0;
 m_awvalid=0;
 m_wdata=0;
 m_wstrb=0;
 m_wlast=0;
 m_wvalid=0;
 m_bready=0;
 m_araddr=0;
 m_arid=0;
 m_arlen=0;
 m_arsize=0;
 m_arburst=0;
 m_arvalid=0;
 m_rready=0;
 m_awaddr=s_awaddr[wo*32+:32];
 m_awlen=s_awlen[wo*8+:8];
 m_awsize=s_awsize[wo*3+:3];
 m_awburst=s_awburst[wo*2+:2];
 m_araddr=s_araddr[ro*32+:32];
 m_arlen=s_arlen[ro*8+:8];
 m_arsize=s_arsize[ro*3+:3];
 m_arburst=s_arburst[ro*2+:2];
 if(ws==1)begin m_awvalid=s_awvalid[wo];s_awready[wo]=m_awready;end
 if(ws==2)begin
  m_wdata=s_wdata[wo*128+:128];m_wstrb=s_wstrb[wo*16+:16];
  m_wvalid=s_wvalid[wo];m_wlast=s_wlast[wo];s_wready[wo]=m_wready;
 end
 if(ws==3)begin
  m_bready=s_bready[wo];s_bvalid[wo]=m_bvalid;
  s_bresp[wo*2+:2]=m_bresp;s_bid[wo*8+:8]=wid;
 end
 if(rs==1)begin m_arvalid=s_arvalid[ro];s_arready[ro]=m_arready;end
 if(read_count!=0)begin
  m_rready=s_rready[response_owner];s_rvalid[response_owner]=m_rvalid;
  s_rdata[response_owner*128+:128]=m_rdata;s_rresp[response_owner*2+:2]=m_rresp;
  s_rid[response_owner*8+:8]=read_id[read_head];s_rlast[response_owner]=m_rlast;
 end
end
always @(posedge clk)begin
 if(rst)begin
  ws<=0;rs<=0;wo<=0;ro<=0;wnext<=0;rnext<=0;wid<=0;
  read_head<=0;read_tail<=0;read_count<=0;
 end
 else begin
  case(ws)
   0: if(wfound)begin wo<=wcandidate;ws<=1;end
   1: if(m_awvalid && m_awready)begin wid<=s_awid[wo*8+:8];ws<=2;end
   2: if(m_wvalid && m_wready && m_wlast)ws<=3;
   3: if(m_bvalid && m_bready)begin ws<=0;wnext<=wo==2?0:wo+1'b1;end
  endcase
  case(rs)
   0: if(rfound && read_count<READ_OUTSTANDING)begin ro<=rcandidate;rs<=1;end
   1: if(read_push)begin rs<=0;rnext<=ro==2?0:ro+1'b1;end
  endcase
  if(read_push)begin
   read_owner[read_tail]<=ro;read_id[read_tail]<=s_arid[ro*8+:8];
   read_tail<=read_tail==READ_OUTSTANDING-1?0:read_tail+1'b1;
  end
  if(read_pop)read_head<=read_head==READ_OUTSTANDING-1?0:read_head+1'b1;
  case({read_push,read_pop})
   2'b10:read_count<=read_count+1'b1;
   2'b01:read_count<=read_count-1'b1;
  endcase
 end
end
endmodule
