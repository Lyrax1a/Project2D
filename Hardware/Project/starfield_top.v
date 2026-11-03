//***************************************************************/
//
// Moudel Name    : ddr3_example_top.v
// Version        : 1.1
// Date Created   : 2023-02-23 10:37:59
// Last Modified  : 2023-02-24 15:27:41
// Abstract       : ---
//
//***************************************************************/
//Modification History
//1.initial
//***************************************************************/
`include "ddr3_controller/ddr3_parameter.vh"

`define Efinity_Debug
`timescale 1ps/1ps
// `define SOFT_JTAG

module starfield_top #
(
parameter                       RANK_RATIO         = 1,       // # of unique CS outputs per rank
parameter                       CK_RATIO           = `CK_RATIO, 
parameter                       ASYN_AXI_CLK       = `ASYN_AXI_CLK, 
parameter                       RANKS              = `RANKS,
parameter                       CK_WIDTH           = `CK_WIDTH,       // # of CK/CK# outputs to memory   
parameter                       CKE_WIDTH          = `CKE_WIDTH,       // # of cke outputs
parameter                       CS_WIDTH           = `CS_WIDTH,       // # of unique CS outputs
parameter                       BANK_WIDTH         = `BANK_WIDTH,       // # of bank bits
parameter                       ROW_WIDTH          = `ROW_WIDTH,       // DRAM address bus width
parameter                       COL_WIDTH          = `COL_WIDTH,      // column address width
parameter                       DM_WIDTH           = `DM_WIDTH,       // # of DM (data mask)
parameter                       DQS_WIDTH          = `DQS_WIDTH,       // # of DQS (strobe)
parameter                       DQ_WIDTH           = `DQ_WIDTH,      // # of DQ (data)
parameter                       ODT_WIDTH          = `ODT_WIDTH,
parameter                       DQ_CNT_WIDTH       = `DQ_CNT_WIDTH,       // = ceil(log2(DQ_WIDTH))
parameter                       DQS_CNT_WIDTH      = `DQS_CNT_WIDTH,       // = ceil(log2(DQS_WIDTH))  
parameter                       DRAM_WIDTH         = `DRAM_WIDTH,       // # of DQ per DQS   
parameter                       DATA_WIDTH         = `DATA_WIDTH,
parameter                       ADDR_WIDTH         = `ADDR_WIDTH,    
parameter                       AXI_ID_WIDTH       = `AXI_ID_WIDTH,
parameter                       AXI_ADDR_WIDTH     = `AXI_ADDR_WIDTH,
parameter                       AXI_DATA_WIDTH     = `AXI_DATA_WIDTH
)
(


   input hdmi_tx_locked,hdmi_tx_slow_clk,
   output [9:0] tmds_data0_o,tmds_data1_o,tmds_data2_o,tmds_clk_o,
   output tmds_data0_TX_OE,tmds_data1_TX_OE,tmds_data2_TX_OE,tmds_clk_TX_OE,
   output tmds_data0_TX_RST,tmds_data1_TX_RST,tmds_data2_TX_RST,tmds_clk_TX_RST,
   // Clock and reset ports
   input                              axi_clk,      
   input                              core_clk,     // CORE CLK @ 100MHz
   input                              sdram_clk,    // SDRAM CK @ 400MHz
   input                              rx_cal_clk,   // SDRAM CK @ 400MHz
   input                              tx_cal_clk,   // SDRAM CK @ 400MHz
   input                              tx_cal_clk_90edge,   // SDRAM CK @ 400MHz
   input                              pll_locked,
   input                              user_pll_locked,
  //  input                              peripheralClk,
   //&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
   output  wire                       system_uart_0_io_txd,
   input                              system_uart_0_io_rxd,   
   input  [3:0]                       soc_gpio_IN,
   output [3:0]                       soc_gpio_OUT,
   output [3:0]                       soc_gpio_OE,
    // debug core ports
`ifdef  Efinity_Debug  //&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&& 
`ifdef SOFT_JTAG
  input                               io_jtag_tms,
  input                               io_jtag_tdi,
  output  wire                        io_jtag_tdo,
  input                               io_jtag_tck,
`else
  input                               jtag_inst1_CAPTURE ,
  input                               jtag_inst1_DRCK    ,
  input                               jtag_inst1_RESET   ,
  input                               jtag_inst1_RUNTEST ,
  input                               jtag_inst1_SEL     ,
  input                               jtag_inst1_SHIFT   ,
  input                               jtag_inst1_TCK     ,
  input                               jtag_inst1_TDI     ,
  input                               jtag_inst1_TMS     ,
  input                               jtag_inst1_UPDATE  ,
  output                              jtag_inst1_TDO     ,
`endif 

`endif  //&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
   // PLL status flags  
   output [2:0]                       pll_shift,  
   output [4:0]                       pll_shift_sel,
   output                             pll_shift_ena,  
   // memory interface ports
   output                             ddr_ck_hi,
   output                             ddr_ck_lo,
   output                             ddr_reset_n,
   output [CKE_WIDTH-1:0]             ddr_cke,     
   output [ROW_WIDTH-1:0]             ddr_addr,
   output [BANK_WIDTH-1:0]            ddr_ba,
   output                             ddr_cas_n,

   output [CS_WIDTH*RANK_RATIO-1:0]   ddr_cs_n,
   output                             ddr_ras_n,
   output                             ddr_we_n,
   
   input  [DQS_WIDTH-1:0]             ddr_dqs_in_hi,
   input  [DQS_WIDTH-1:0]             ddr_dqs_in_lo,
   input  [DQ_WIDTH-1:0]              ddr_dq_in_hi,
   input  [DQ_WIDTH-1:0]              ddr_dq_in_lo,
   
   output [DQS_WIDTH-1:0]             ddr_dqs_oe,
   output [DQS_WIDTH-1:0]             ddr_dqs_oe_n,
   output [DQ_WIDTH-1:0]              ddr_dq_oe,  
   output [DQS_WIDTH-1:0]             ddr_dqs_out_hi,
   output [DQS_WIDTH-1:0]             ddr_dqs_out_lo,
   output [DQ_WIDTH-1:0]              ddr_dq_out_hi,
   output [DQ_WIDTH-1:0]              ddr_dq_out_lo,
   output [DM_WIDTH-1:0]              ddr_dm_hi,
   output [DM_WIDTH-1:0]              ddr_dm_lo,
   output [ODT_WIDTH-1:0]             ddr_odt,

   input                              system_spi_0_io_data_0_IN,
   input                              system_spi_0_io_data_1_IN,
   output                             system_spi_0_io_ss,
   output                             system_spi_0_io_sclk_write,
   output                             system_spi_0_io_data_0_OUT,
   output                             system_spi_0_io_data_0_OE,
   output                             system_spi_0_io_data_1_OUT,
   output                             system_spi_0_io_data_1_OE

   );


      
//Parameter Define
parameter FREQ = 200;			// default is 100 MHz.  Redefine as needed.
`ifndef SIM
    localparam CNT_INIT = 1.5*FREQ*1000;
`else
    localparam CNT_INIT = 10;
`endif    
  localparam   AIW               = AXI_ID_WIDTH      ;
  localparam   ADW               = AXI_DATA_WIDTH    ;
  localparam   ABN               = AXI_DATA_WIDTH/8   ;
  /////////////////////////////////////////////////////////
  // Wire declarations
  reg  [19:0]                       cnt;    
  wire                              clk;
  wire                              rst;
  wire                              mmcm_locked;
  reg                               aresetn;
  wire                              app_sr_active;
  wire                              app_ref_ack;
  wire                              app_zq_ack;

  wire                              cal_done;

  // Slave Interface Write Address Ports
  wire [AXI_ID_WIDTH-1:0]           s_axi_awid;
  wire [AXI_ADDR_WIDTH-1:0]         s_axi_awaddr;
  wire [7:0]                        s_axi_awlen;
  wire [2:0]                        s_axi_awsize;
  wire [1:0]                        s_axi_awburst;
  wire [0:0]                        s_axi_awlock;
  wire [3:0]                        s_axi_awcache;
  wire [2:0]                        s_axi_awprot;
  wire                              s_axi_awvalid;
  wire                              s_axi_awready;
   // Slave Interface Write Data Ports
  wire [AXI_DATA_WIDTH-1:0]         s_axi_wdata;
  wire [(AXI_DATA_WIDTH/8)-1:0]     s_axi_wstrb;
  wire                              s_axi_wlast;
  wire                              s_axi_wvalid;
  wire                              s_axi_wready;
   // Slave Interface Write Response Ports
  wire                              s_axi_bready;
  wire [AXI_ID_WIDTH-1:0]           s_axi_bid;
  wire [1:0]                        s_axi_bresp;
  wire                              s_axi_bvalid;
   // Slave Interface Read Address Ports
  wire [AXI_ID_WIDTH-1:0]           s_axi_arid;
  wire [AXI_ADDR_WIDTH-1:0]         s_axi_araddr;
  wire [7:0]                        s_axi_arlen;
  wire [2:0]                        s_axi_arsize;
  wire [1:0]                        s_axi_arburst;
  wire [0:0]                        s_axi_arlock;
  wire [3:0]                        s_axi_arcache;
  wire [2:0]                        s_axi_arprot;
  wire                              s_axi_arvalid;
  wire                              s_axi_arready;
   // Slave Interface Read Data Ports
  wire                              s_axi_rready;
  wire [AXI_ID_WIDTH-1:0]           s_axi_rid;
  wire [AXI_DATA_WIDTH-1:0]         s_axi_rdata;
  wire [1:0]                        s_axi_rresp;
  wire                              s_axi_rlast;
  wire                              s_axi_rvalid;


//***************************************************************************
  wire [2:0]                        vio_pll_shift;  
  wire [4:0]                        vio_pll_shift_sel;
  wire                              vio_pll_shift_ena; 
  wire                              pos_pll_shift_ena; 
  wire [2:0]                        phy_pll_shift;  
  wire [4:0]                        phy_pll_shift_sel;
  wire                              phy_pll_shift_ena; 
  wire                              ddr_reset; 
  wire                              sys_rst;
  wire  [2:0]                       phy_wr_pll_shift;
  wire                              idelay_ld       ;
  wire                              mpr_rdlvl_dly   ;
  wire  [7:0]                       wrlvl_dq_check  ;
  wire  [7:0]                       rd_level_dqs_check;  
  wire  [2:0]                       rdlvl_shift;  
  wire  [2:0]                       wrlvl_shift;  
  wire  [15:0]                      debug_fifo;  
  wire  [15:0]                      overflow_fifo;  
  wire  [6:0]                       init_cur_state;  
  wire  [35:0]                      ddr_debug_port;  
  wire                              app_rdy;  
  wire                              user_clk;  
  wire                              ddr_rstn;  
  wire                                phy_rddata_valid;
  wire [2*CK_RATIO*DQ_WIDTH-1:0]      phy_rd_data;
  wire [2*CK_RATIO*DATA_WIDTH-1:0]    app_rd_data_to_axi; 
  wire                                app_rd_data_end;
  wire                                app_rd_data_valid;
  wire [2*CK_RATIO*DQ_WIDTH-1:0]      rd_data;          
  wire                                rd_data_en;            
  wire                                rd_data_end;   
   
  
// Start of User Design top instance
//***************************************************************************
// The User design is instantiated below. The memory interface ports are
// connected to the top-level and the application interface ports are
// connected to the traffic generator module. This provides a reference
// for connecting the memory controller to system.
//***************************************************************************
always @(posedge user_clk or negedge sys_rst) begin
	if (!sys_rst)
		cnt <= 0;
    else if (cnt != CNT_INIT) 
        cnt <= cnt + 20'd1;
	else 
        cnt <= cnt;
end
assign ddr_rstn = (cnt == CNT_INIT);


generate
if (ASYN_AXI_CLK) begin
assign user_clk = axi_clk;

end else begin
assign user_clk = core_clk;
end
endgenerate
//***************************************************************************
// The traffic generation module instantiated below drives traffic (patterns)
// on the application interface of the memory controller
//***************************************************************************
//***************************************************************************

assign sys_rst       = pll_locked & user_pll_locked;

//***************************************************************************
ddr3_top                 u_ddr3_top
      (
      
    .axi_clk                (user_clk             ),
    .core_clk               (core_clk            ),
    .sdram_clk              (sdram_clk           ),  
    .rx_cal_clk             (rx_cal_clk          ),
    .tx_cal_clk             (tx_cal_clk          ),
    .tx_cal_clk_90edge      (tx_cal_clk_90edge   ),
    .rstn                   (ddr_rstn            ),      
    .pll_shift              (pll_shift           ),
    .pll_shift_sel          (pll_shift_sel       ),
    .pll_shift_ena          (pll_shift_ena       ), 
///////////////DDR BUS
    .ddr_ck_hi              (ddr_ck_hi           ),
    .ddr_ck_lo              (ddr_ck_lo           ),
    .ddr_cke                (ddr_cke             ),    
    .ddr_reset_n            (ddr_reset_n         ),
    .ddr_cs_n               (ddr_cs_n            ),
    .ddr_ras_n              (ddr_ras_n           ),
    .ddr_cas_n              (ddr_cas_n           ),
    .ddr_we_n               (ddr_we_n            ),     
    .ddr_addr               (ddr_addr            ),
    .ddr_ba                 (ddr_ba              ),

    .ddr_dqs_oe             (ddr_dqs_oe          ),
    .ddr_dqs_oe_n           (ddr_dqs_oe_n        ),
    .ddr_dq_oe              (ddr_dq_oe           ),
    .ddr_dqs_in_hi          (ddr_dqs_in_hi       ),
    .ddr_dqs_in_lo          (ddr_dqs_in_lo       ),
    .ddr_dq_in_hi           (ddr_dq_in_hi        ),
    .ddr_dq_in_lo           (ddr_dq_in_lo        ),

    .ddr_dqs_out_hi         (ddr_dqs_out_hi      ),
    .ddr_dqs_out_lo         (ddr_dqs_out_lo      ),
    .ddr_dq_out_hi          (ddr_dq_out_hi       ),
    .ddr_dq_out_lo          (ddr_dq_out_lo       ),
    
    .ddr_dm_hi              (ddr_dm_hi           ),
    .ddr_dm_lo              (ddr_dm_lo           ),
    .ddr_odt                (ddr_odt             ),

// Application interface ports
       .app_sr_req                     (1'b0),
       .app_ref_req                    (1'b0),
       .app_zq_req                     (1'b0),
       .app_sr_active                  (app_sr_active),
       .app_ref_ack                    (app_ref_ack),
       .app_zq_ack                     (app_zq_ack),

// Slave Interface Write Address Ports
       .s_axi_awid                     (s_axi_awid        ),
       .s_axi_awaddr                   (s_axi_awaddr      ),
       .s_axi_awlen                    (s_axi_awlen       ),
       .s_axi_awsize                   (s_axi_awsize      ),
       .s_axi_awburst                  (s_axi_awburst     ),
       .s_axi_awlock                   (s_axi_awlock      ),
       .s_axi_awcache                  (s_axi_awcache     ),
       .s_axi_awprot                   (s_axi_awprot      ),
       .s_axi_awqos                    (4'h0              ),
       .s_axi_awvalid                  (s_axi_awvalid     ),
       .s_axi_awready                  (s_axi_awready     ),
// Slave Interface Write Data Ports
       .s_axi_wdata                    (s_axi_wdata       ),
       .s_axi_wstrb                    (s_axi_wstrb       ),
       .s_axi_wlast                    (s_axi_wlast       ),
       .s_axi_wvalid                   (s_axi_wvalid      ),
       .s_axi_wready                   (s_axi_wready      ),
// Slave Interface Write Response Ports
       .s_axi_bid                      (s_axi_bid         ),
       .s_axi_bresp                    (s_axi_bresp       ),
       .s_axi_bvalid                   (s_axi_bvalid      ),
       .s_axi_bready                   (s_axi_bready      ),
// Slave Interface Read Address Ports
       .s_axi_arid                     (s_axi_arid        ),
       .s_axi_araddr                   (s_axi_araddr      ),
       .s_axi_arlen                    (s_axi_arlen       ),
       .s_axi_arsize                   (s_axi_arsize      ),
       .s_axi_arburst                  (s_axi_arburst     ),
       .s_axi_arlock                   (s_axi_arlock      ),
       .s_axi_arcache                  (s_axi_arcache     ),
       .s_axi_arprot                   (s_axi_arprot      ),
       .s_axi_arqos                    (4'h0              ),
       .s_axi_arvalid                  (s_axi_arvalid     ),
       .s_axi_arready                  (s_axi_arready     ),
// Slave Interface Read Data Ports
       .s_axi_rid                      (s_axi_rid         ),
       .s_axi_rdata                    (s_axi_rdata       ),
       .s_axi_rresp                    (s_axi_rresp       ),
       .s_axi_rlast                    (s_axi_rlast       ),
       .s_axi_rvalid                   (s_axi_rvalid      ),
       .s_axi_rready                   (s_axi_rready      ),
//DEBUG       
       .wrlvl_dq_check                 (wrlvl_dq_check    ) ,
       .rd_level_dqs_check             (rd_level_dqs_check) ,
       .rdlvl_shift                    (rdlvl_shift) ,
       .wrlvl_shift                    (wrlvl_shift) ,
       .init_cur_state                 (init_cur_state    ) ,
       .idelay_ld                      (idelay_ld         ) ,
       .mpr_rdlvl_dly                  (mpr_rdlvl_dly     ) ,
       .ddr_debug_port                 (ddr_debug_port    ) ,
       .cal_done                       (cal_done          ) 
       );
// End of User Design top instance

//***************************************************************************


//***************************************************************************
wire [31:0] cpu_awaddr;
wire [7:0] cpu_awid;
wire [7:0] cpu_awlen;
wire [2:0] cpu_awsize;
wire [1:0] cpu_awburst;
wire [0:0] cpu_awvalid;
wire [127:0] cpu_wdata;
wire [15:0] cpu_wstrb;
wire [0:0] cpu_wlast;
wire [0:0] cpu_wvalid;
wire [0:0] cpu_bready;
wire [31:0] cpu_araddr;
wire [7:0] cpu_arid;
wire [7:0] cpu_arlen;
wire [2:0] cpu_arsize;
wire [1:0] cpu_arburst;
wire [0:0] cpu_arvalid;
wire [0:0] cpu_rready;
wire [0:0] cpu_awready;
wire [0:0] cpu_wready;
wire [1:0] cpu_bresp;
wire [7:0] cpu_bid;
wire [0:0] cpu_bvalid;
wire [0:0] cpu_arready;
wire [127:0] cpu_rdata;
wire [7:0] cpu_rid;
wire [1:0] cpu_rresp;
wire [0:0] cpu_rlast;
wire [0:0] cpu_rvalid;
wire [31:0] mem_awaddr;
wire [7:0] mem_awid;
wire [7:0] mem_awlen;
wire [2:0] mem_awsize;
wire [1:0] mem_awburst;
wire [0:0] mem_awvalid;
wire [127:0] mem_wdata;
wire [15:0] mem_wstrb;
wire [0:0] mem_wlast;
wire [0:0] mem_wvalid;
wire [0:0] mem_bready;
wire [31:0] mem_araddr;
wire [7:0] mem_arid;
wire [7:0] mem_arlen;
wire [2:0] mem_arsize;
wire [1:0] mem_arburst;
wire [0:0] mem_arvalid;
wire [0:0] mem_rready;
wire [0:0] mem_awready;
wire [0:0] mem_wready;
wire [1:0] mem_bresp;
wire [7:0] mem_bid;
wire [0:0] mem_bvalid;
wire [0:0] mem_arready;
wire [127:0] mem_rdata;
wire [7:0] mem_rid;
wire [1:0] mem_rresp;
wire [0:0] mem_rlast;
wire [0:0] mem_rvalid;
wire [15:0] paddr;wire psel,penable,pwrite,pready,pslverror,irq;
wire [31:0] pwdata,prdata,sys_prdata;wire soc_reset,memory_reset;
assign prdata=(paddr==16'h420) ? {31'b0,cal_done} : sys_prdata;
assign s_axi_awlock=0;assign s_axi_awcache=0;assign s_axi_awprot=0;
assign s_axi_arlock=0;assign s_axi_arcache=0;assign s_axi_arprot=0;
assign s_axi_awaddr=mem_awaddr[27:0];
assign s_axi_awid=mem_awid[3:0];
assign s_axi_awlen=mem_awlen;
assign s_axi_awsize=mem_awsize;
assign s_axi_awburst=mem_awburst;
assign s_axi_awvalid=mem_awvalid;
assign s_axi_wdata=mem_wdata;
assign s_axi_wstrb=mem_wstrb;
assign s_axi_wlast=mem_wlast;
assign s_axi_wvalid=mem_wvalid;
assign s_axi_bready=mem_bready;
assign s_axi_araddr=mem_araddr[27:0];
assign s_axi_arid=mem_arid[3:0];
assign s_axi_arlen=mem_arlen;
assign s_axi_arsize=mem_arsize;
assign s_axi_arburst=mem_arburst;
assign s_axi_arvalid=mem_arvalid;
assign s_axi_rready=mem_rready;
assign mem_awready=s_axi_awready;
assign mem_wready=s_axi_wready;
assign mem_bresp=s_axi_bresp;
assign mem_bid={4'b0,s_axi_bid};
assign mem_bvalid=s_axi_bvalid;
assign mem_arready=s_axi_arready;
assign mem_rdata=s_axi_rdata;
assign mem_rid={4'b0,s_axi_rid};
assign mem_rresp=s_axi_rresp;
assign mem_rlast=s_axi_rlast;
assign mem_rvalid=s_axi_rvalid;
soc cpu(
.io_systemClk(user_clk),
.io_ddrA_w_payload_strb(cpu_wstrb),
.io_ddrA_w_payload_data(cpu_wdata),
.axiA_awready(1'd0),
.axiA_awlen(),
.axiA_awsize(),
.axiA_arburst(),
.axiA_awlock(),
.axiA_arcache(),
.axiA_awqos(),
.axiA_awprot(),
.axiA_arsize(),
.axiA_arregion(),
.axiA_arready(1'd0),
.axiA_arqos(),
.axiA_arprot(),
.axiA_arlock(),
.axiA_arlen(),
.axiA_arid(),
.axiA_awcache(),
.axiA_awburst(),
.axiA_awaddr(),
.axiAInterrupt(1'd0),
.axiA_rlast(1'd0),
.jtagCtrl_enable(jtag_inst1_SEL),
.jtagCtrl_tdi(jtag_inst1_TDI),
.jtagCtrl_capture(jtag_inst1_CAPTURE),
.jtagCtrl_shift(jtag_inst1_SHIFT),
.jtagCtrl_update(jtag_inst1_UPDATE),
.jtagCtrl_reset(jtag_inst1_RESET),
.jtagCtrl_tdo(jtag_inst1_TDO),
.jtagCtrl_tck(jtag_inst1_TCK),
.axiA_araddr(),
.axiA_wvalid(),
.axiA_wready(1'd0),
.axiA_wdata(),
.axiA_wstrb(),
.axiA_wlast(),
.axiA_bvalid(1'd0),
.axiA_bready(),
.axiA_bid(8'd0),
.axiA_bresp(2'd0),
.axiA_rvalid(1'd0),
.axiA_rready(),
.axiA_rdata(32'd0),
.axiA_rid(8'd0),
.axiA_rresp(2'd0),
.axiA_arvalid(),
.axiA_awid(),
.axiA_awregion(),
.axiA_awvalid(),
.io_ddrA_r_payload_last(cpu_rlast),
.io_ddrA_r_payload_resp(cpu_rresp),
.io_ddrA_r_payload_id(cpu_rid),
.io_ddrA_r_payload_data(cpu_rdata),
.io_ddrA_r_ready(cpu_rready),
.io_ddrA_r_valid(cpu_rvalid),
.io_ddrA_b_payload_resp(cpu_bresp),
.io_ddrA_b_payload_id(cpu_bid),
.io_ddrA_b_ready(cpu_bready),
.io_ddrA_b_valid(cpu_bvalid),
.io_ddrA_w_payload_last(cpu_wlast),
.io_ddrA_w_ready(cpu_wready),
.io_ddrA_w_valid(cpu_wvalid),
.io_ddrA_aw_payload_prot(),
.io_ddrA_aw_payload_qos(),
.io_ddrA_aw_payload_cache(),
.io_ddrA_aw_payload_lock(),
.io_ddrA_aw_payload_burst(cpu_awburst),
.io_ddrA_aw_payload_size(cpu_awsize),
.io_ddrA_aw_payload_len(cpu_awlen),
.io_ddrA_aw_payload_region(),
.io_ddrA_aw_payload_id(cpu_awid),
.io_ddrA_aw_payload_addr(cpu_awaddr),
.io_ddrA_aw_ready(cpu_awready),
.io_ddrA_aw_valid(cpu_awvalid),
.io_ddrA_ar_payload_prot(),
.io_ddrA_ar_payload_qos(),
.io_ddrA_ar_payload_cache(),
.io_ddrA_ar_payload_lock(),
.io_ddrA_ar_payload_burst(cpu_arburst),
.io_ddrA_ar_payload_size(cpu_arsize),
.io_ddrA_ar_payload_len(cpu_arlen),
.io_ddrA_ar_payload_region(),
.io_ddrA_ar_payload_id(cpu_arid),
.io_ddrA_ar_payload_addr(cpu_araddr),
.io_ddrA_ar_ready(cpu_arready),
.io_ddrA_ar_valid(cpu_arvalid),
.system_spi_0_io_data_0_read(1'd1),
.system_spi_0_io_data_0_write(),
.system_spi_0_io_data_0_writeEnable(),
.system_spi_0_io_data_1_read(1'd1),
.system_spi_0_io_data_1_write(),
.system_spi_0_io_data_1_writeEnable(),
.system_spi_0_io_data_2_read(1'd1),
.system_spi_0_io_data_2_write(),
.system_spi_0_io_data_2_writeEnable(),
.system_spi_0_io_data_3_read(1'd1),
.system_spi_0_io_data_3_write(),
.system_spi_0_io_data_3_writeEnable(),
.system_spi_0_io_sclk_write(),
.userInterruptA(irq),
.io_apbSlave_0_PADDR(paddr),
.io_apbSlave_0_PENABLE(penable),
.io_apbSlave_0_PRDATA(prdata),
.io_apbSlave_0_PREADY(pready),
.io_apbSlave_0_PSEL(psel),
.io_apbSlave_0_PSLVERROR(pslverror),
.io_apbSlave_0_PWDATA(pwdata),
.io_apbSlave_0_PWRITE(pwrite),
.io_asyncReset(!ddr_rstn),
.io_memoryClk(user_clk),
.io_systemReset(soc_reset),
.system_uart_0_io_txd(system_uart_0_io_txd),
.io_memoryReset(memory_reset),
.system_uart_0_io_rxd(system_uart_0_io_rxd),
.system_i2c_0_io_scl_read(1'd1),
.system_i2c_0_io_scl_write(),
.system_i2c_0_io_sda_read(1'd1),
.system_i2c_0_io_sda_write(),
.system_gpio_0_io_writeEnable(),
.io_ddrMasters_0_reset(),
.io_ddrMasters_0_clk(user_clk),
.io_ddrMasters_0_r_payload_last(),
.io_ddrMasters_0_r_payload_resp(),
.io_ddrMasters_0_r_payload_id(),
.io_ddrMasters_0_r_payload_data(),
.io_ddrMasters_0_r_ready(1'd0),
.io_ddrMasters_0_r_valid(),
.io_ddrMasters_0_ar_payload_prot(3'd0),
.io_ddrMasters_0_ar_payload_qos(4'd0),
.io_ddrMasters_0_ar_payload_cache(4'd0),
.io_ddrMasters_0_ar_payload_lock(1'd0),
.io_ddrMasters_0_ar_payload_burst(2'd0),
.io_ddrMasters_0_ar_payload_size(3'd0),
.io_ddrMasters_0_ar_payload_len(8'd0),
.io_ddrMasters_0_ar_payload_region(4'd0),
.io_ddrMasters_0_ar_payload_id(4'd0),
.io_ddrMasters_0_ar_payload_addr(32'd0),
.io_ddrMasters_0_ar_ready(),
.io_ddrMasters_0_ar_valid(1'd0),
.io_ddrMasters_0_b_payload_resp(),
.io_ddrMasters_0_b_payload_id(),
.io_ddrMasters_0_b_ready(1'd0),
.io_ddrMasters_0_b_valid(),
.io_ddrMasters_0_w_payload_last(1'd0),
.io_ddrMasters_0_w_payload_strb(4'd0),
.io_ddrMasters_0_w_payload_data(32'd0),
.io_ddrMasters_0_w_ready(),
.io_ddrMasters_0_w_valid(1'd0),
.io_ddrMasters_0_aw_payload_prot(3'd0),
.io_ddrMasters_0_aw_payload_qos(4'd0),
.io_ddrMasters_0_aw_payload_cache(4'd0),
.io_ddrMasters_0_aw_payload_lock(1'd0),
.io_ddrMasters_0_aw_payload_burst(2'd0),
.io_ddrMasters_0_aw_payload_size(3'd0),
.io_ddrMasters_0_aw_payload_len(8'd0),
.io_ddrMasters_0_aw_payload_region(4'd0),
.io_ddrMasters_0_aw_payload_id(4'd0),
.io_ddrMasters_0_aw_payload_addr(32'd0),
.io_ddrMasters_0_aw_ready(),
.io_ddrMasters_0_aw_valid(1'd0),
.system_gpio_0_io_write(),
.system_gpio_0_io_read(4'd1),
.system_spi_0_io_ss());
wire pixel_rst_n;wire hs,vs,de;wire [7:0] red,green,blue;
wire [23:0] audio_l,audio_r;wire audio_valid;
reset #(.IN_RST_ACTIVE("LOW"),.OUT_RST_ACTIVE("LOW"),.CYCLE(3)) pix_reset
 (.i_arst(hdmi_tx_locked),.i_clk(hdmi_tx_slow_clk),.o_srst(pixel_rst_n));
ts2d_system system(.clk(user_clk),.rst(soc_reset || !ddr_rstn),
 .pixel_clk(hdmi_tx_slow_clk),.pixel_rst(!pixel_rst_n),
 .audio_l(audio_l),.audio_r(audio_r),.audio_valid(audio_valid),
 .paddr(paddr),.psel(psel),.penable(penable),.pwrite(pwrite),.pwdata(pwdata),
 .prdata(sys_prdata),.pready(pready),.pslverror(pslverror),.irq(irq),
 .hs(hs),.vs(vs),.de(de),.red(red),.green(green),.blue(blue),
.cpu_awaddr(cpu_awaddr),
.cpu_awid(cpu_awid),
.cpu_awlen(cpu_awlen),
.cpu_awsize(cpu_awsize),
.cpu_awburst(cpu_awburst),
.cpu_awvalid(cpu_awvalid),
.cpu_wdata(cpu_wdata),
.cpu_wstrb(cpu_wstrb),
.cpu_wlast(cpu_wlast),
.cpu_wvalid(cpu_wvalid),
.cpu_bready(cpu_bready),
.cpu_araddr(cpu_araddr),
.cpu_arid(cpu_arid),
.cpu_arlen(cpu_arlen),
.cpu_arsize(cpu_arsize),
.cpu_arburst(cpu_arburst),
.cpu_arvalid(cpu_arvalid),
.cpu_rready(cpu_rready),
.cpu_awready(cpu_awready),
.cpu_wready(cpu_wready),
.cpu_bresp(cpu_bresp),
.cpu_bid(cpu_bid),
.cpu_bvalid(cpu_bvalid),
.cpu_arready(cpu_arready),
.cpu_rdata(cpu_rdata),
.cpu_rid(cpu_rid),
.cpu_rresp(cpu_rresp),
.cpu_rlast(cpu_rlast),
.cpu_rvalid(cpu_rvalid),
.mem_awaddr(mem_awaddr),
.mem_awid(mem_awid),
.mem_awlen(mem_awlen),
.mem_awsize(mem_awsize),
.mem_awburst(mem_awburst),
.mem_awvalid(mem_awvalid),
.mem_wdata(mem_wdata),
.mem_wstrb(mem_wstrb),
.mem_wlast(mem_wlast),
.mem_wvalid(mem_wvalid),
.mem_bready(mem_bready),
.mem_araddr(mem_araddr),
.mem_arid(mem_arid),
.mem_arlen(mem_arlen),
.mem_arsize(mem_arsize),
.mem_arburst(mem_arburst),
.mem_arvalid(mem_arvalid),
.mem_rready(mem_rready),
.mem_awready(mem_awready),
.mem_wready(mem_wready),
.mem_bresp(mem_bresp),
.mem_bid(mem_bid),
.mem_bvalid(mem_bvalid),
.mem_arready(mem_arready),
.mem_rdata(mem_rdata),
.mem_rid(mem_rid),
.mem_rresp(mem_rresp),
.mem_rlast(mem_rlast),
.mem_rvalid(mem_rvalid));
wire [9:0] t0,t1,t2,tc;
dvi_encoder enc(.pixelclk(hdmi_tx_slow_clk),.rst_p(!pixel_rst_n),
 .i_bdata(blue),.i_gdata(green),.i_rdata(red),.i_hs(hs),.i_vs(vs),.i_de(de),
 .video_format(2'd0),.video_VIC(7'd16),.audio_L(audio_l),.audio_R(audio_r),.audio_valid(audio_valid),
 .audio_N(20'd6144),.audio_CTS(20'd148750),.audio_sample_frequency(4'b0010),.audio_word_length(4'b1011),
 .tmds_data0(t0),.tmds_data1(t1),.tmds_data2(t2),.tmds_clk(tc));
assign tmds_data0_o=~t0;assign tmds_data1_o=~t1;assign tmds_data2_o=~t2;assign tmds_clk_o=~tc;
assign {tmds_data0_TX_OE,tmds_data1_TX_OE,tmds_data2_TX_OE,tmds_clk_TX_OE}=4'b1111;
assign {tmds_data0_TX_RST,tmds_data1_TX_RST,tmds_data2_TX_RST,tmds_clk_TX_RST}=4'b0;
assign soc_gpio_OUT=0;assign soc_gpio_OE=0;
assign system_spi_0_io_ss=1;assign system_spi_0_io_sclk_write=0;
assign system_spi_0_io_data_0_OUT=0;assign system_spi_0_io_data_0_OE=0;
assign system_spi_0_io_data_1_OUT=0;assign system_spi_0_io_data_1_OE=0;
endmodule
