/* -*- c++ -*- */
/*
 * Copyright 2013 Dimitri Stolnikov <horiz0n@gmx.net>
 *
 * GNU Radio is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * GNU Radio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

/*
 * config.h is generated by configure.  It contains the results
 * of probing for feature_t, options etc.  It should be the first
 * file included in your .cc file.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <iostream>

#include <boost/assign.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/asio/deadline_timer.hpp>

#include <gnuradio/io_signature.h>

#include "arg_helpers.h"
#include "netsdr_source_c.h"

using namespace boost::assign;
using boost::asio::deadline_timer;

#define DEFAULT_HOST  "127.0.0.1" /* We assume a running moetronix server */
#define DEFAULT_PORT  50000

/*
 * Create a new instance of netsdr_source_c and return
 * a boost shared_ptr.  This is effectively the public constructor.
 */
netsdr_source_c_sptr make_netsdr_source_c (const std::string &args)
{
  return gnuradio::get_initial_sptr(new netsdr_source_c (args));
}

/*
 * Specify constraints on number of input and output streams.
 * This info is used to construct the input and output signatures
 * (2nd & 3rd args to gr_block's constructor).  The input and
 * output signatures are used by the runtime system to
 * check that a valid number and type of inputs and outputs
 * are connected to this block.  In this case, we accept
 * only 0 input and 1 output.
 */
static const int MIN_IN = 0;	// mininum number of input streams
static const int MAX_IN = 0;	// maximum number of input streams
static const int MIN_OUT = 1;	// minimum number of output streams
static const int MAX_OUT = 1;	// maximum number of output streams

/*
 * The private constructor
 */
netsdr_source_c::netsdr_source_c (const std::string &args)
  : gr::sync_block ("netsdr_source_c",
                    gr::io_signature::make (MIN_IN, MAX_IN, sizeof (gr_complex)),
                    gr::io_signature::make (MIN_OUT, MAX_OUT, sizeof (gr_complex))),
    _io_service(),
    _resolver(_io_service),
    _t(_io_service),
    _u(_io_service),
    _running(false),
    _sequence(0),
    _nchan(1)
{
  std::string host = "";
  unsigned short port = 0;

  dict_t dict = params_to_dict(args);

  if (dict.count("netsdr")) {
    std::string value = dict["netsdr"];

    if ( ! value.length() )
    {
      std::vector< std::string > devices = get_devices();

      if ( devices.size() )
      {
        dict_t first = params_to_dict( devices[0] );

        dict["netsdr"] = value = first["netsdr"];
        dict["label"] = first["label"];
      }
    }

    std::vector< std::string > tokens;
    boost::algorithm::split( tokens, value, boost::is_any_of(":") );

    if ( tokens[0].length() && (tokens.size() == 1 || tokens.size() == 2 ) )
      host = tokens[0];

    if ( tokens.size() == 2 ) // port given
      port = boost::lexical_cast< unsigned short >( tokens[1] );
  }

  if (dict.count("nchan"))
    _nchan = boost::lexical_cast< size_t >( dict["nchan"] );

  if ( _nchan < 1 || _nchan > 2 )
    throw std::runtime_error("Number of channels (nchan) must be 1 or 2");

  if ( ! host.length() )
    host = DEFAULT_HOST;

  if (0 == port)
    port = DEFAULT_PORT;

  std::string port_str = boost::lexical_cast< std::string >( port );

  std::string label = dict["label"];

  if ( label.length() )
    std::cerr << "Using " + label << " ";

  tcp::resolver::query query(tcp::v4(), host.c_str(), port_str.c_str());
  tcp::resolver::iterator iterator = _resolver.resolve(query);

  boost::system::error_code ec;

  boost::asio::connect(_t, iterator, ec);
  if ( ec )
    throw std::runtime_error(ec.message() + " (" + host + ":" + port_str + ")");

  _u.open(udp::v4(), ec);
  if ( ec )
    throw std::runtime_error(ec.message());

  // TODO: make listener port dynamic
  _u.bind(udp::endpoint(udp::v4(), DEFAULT_PORT), ec);
    if ( ec )
      throw std::runtime_error(ec.message());

  _u.set_option(udp::socket::reuse_address(true));
  _t.set_option(udp::socket::reuse_address(true));

  // request & print device information

  std::vector< unsigned char > response;

  if ( ! label.length() ) /* label is empty, request name & sn from device */
  {
    std::cerr << "Using ";

    unsigned char name[4] = { 0x04, 0x20, 0x01, 0x00 };
    if ( transaction( name, sizeof(name), response ) )
      std::cerr << &response[sizeof(name)] << " ";

    unsigned char sern[4] = { 0x04, 0x20, 0x02, 0x00 };
    if ( transaction( sern, sizeof(sern), response ) )
      std::cerr << &response[sizeof(sern)] << " ";
  }

  bool has_X2_option = false;

  unsigned char opts[4] = { 0x04, 0x20, 0x0A, 0x00 };
  if ( transaction( opts, sizeof(opts), response ) )
  {
    if ( response[sizeof(opts)] )
    {
      has_X2_option = (response[sizeof(opts)] & 16 ? true : false);

      std::cerr << "option ";
      std::cerr << (response[sizeof(opts)] & 16 ? "2" : "-"); /* X2 board */
      std::cerr << (response[sizeof(opts)] &  8 ? "U" : "-"); /* Up Converter */
      std::cerr << (response[sizeof(opts)] &  4 ? "D" : "-"); /* Down Converter */
      std::cerr << (response[sizeof(opts)] &  2 ? "R" : "-"); /* Reflock board */
      std::cerr << (response[sizeof(opts)] &  1 ? "S" : "-"); /* Sound Enabled */
      std::cerr << " ";
    }
  }

  unsigned char bootver[5] = { 0x05, 0x20, 0x04, 0x00, 0x00 };
  if ( transaction( bootver, sizeof(bootver), response ) )
    std::cerr << "BOOT " << *((uint16_t *)&response[sizeof(bootver)]) << " ";

  unsigned char firmver[5] = { 0x05, 0x20, 0x04, 0x00, 0x01 };
  if ( transaction( firmver, sizeof(firmver), response ) )
    std::cerr << "FW " << *((uint16_t *)&response[sizeof(firmver)]) << " ";

  unsigned char hardver[5] = { 0x05, 0x20, 0x04, 0x00, 0x02 };
  if ( transaction( hardver, sizeof(hardver), response ) )
    std::cerr << "HW " << *((uint16_t *)&response[sizeof(hardver)]) << " ";

  unsigned char fpgaver[5] = { 0x05, 0x20, 0x04, 0x00, 0x03 };
  if ( transaction( fpgaver, sizeof(fpgaver), response ) )
    std::cerr << "FPGA " << int(response[sizeof(fpgaver)])
              << "/" << int(response[sizeof(fpgaver)+1]) << " ";

  std::cerr << std::endl;

  {
    /* 4.2.2 Receiver Channel Setup */
    unsigned char rxchan[5] = { 0x05, 0x00, 0x19, 0x00, 0x00 };

    unsigned char mode = 0; /* 0 = Single Channel Mode */

    if ( 2 == _nchan )
    {
      if ( has_X2_option )
        mode = 6; /* Dual Channel with dual A/D RF Path (requires X2 option) */
      else
        mode = 4; /* Dual Channel with single A/D RF Path using main A/D. */

      set_output_signature( gr::io_signature::make (2, 2, sizeof (gr_complex)) );
    }

    rxchan[sizeof(rxchan)-1] = mode;
    transaction( rxchan, sizeof(rxchan) );
  }

  set_sample_rate( 500e3 );

  set_bandwidth( 0 ); /* switch to automatic filter selection by default */
}

/*
 * Our virtual destructor.
 */
netsdr_source_c::~netsdr_source_c ()
{

}

void netsdr_source_c::apply_channel( unsigned char *cmd, size_t chan_pos, size_t chan )
{
  if ( 0 == chan )
  {
    cmd[chan_pos] = 0;
  }
  else if ( 1 == chan )
  {
    if ( _nchan < 2 )
      throw std::runtime_error("Channel must be 0 or 1");

    cmd[chan_pos] = 2;
  }
  else
    throw std::runtime_error("Channel must be 0 or 1");
}

bool netsdr_source_c::transaction( const unsigned char *cmd, size_t size )
{
  std::vector< unsigned char > response;

  if ( ! transaction( cmd, size, response ) )
    return false;

  /* comparing the contents is not really feasible due to protocol */
  if ( response.size() == size ) /* check response size against request */
    return true;

  return false;
}

//#define VERBOSE

bool netsdr_source_c::transaction( const unsigned char *cmd, size_t size,
                                   std::vector< unsigned char > &response )
{
  unsigned char data[1024*2];
  response.clear();

#ifdef VERBOSE
  printf("< ");
  for (size_t i = 0; i < size; i++)
    printf("%02x ", (unsigned char) cmd[i]);
  printf("\n");
#endif

  _t.write_some( boost::asio::buffer(cmd, size) );

  size_t rx_bytes = _t.read_some( boost::asio::buffer(data, sizeof(data)) );

  response.resize( rx_bytes );
  memcpy( response.data(), data, rx_bytes );

#ifdef VERBOSE
  printf("> ");
  for (size_t i = 0; i < rx_bytes; i++)
    printf("%02x ", (unsigned char) data[i]);
  printf("\n");
#endif

  return true;
}

bool netsdr_source_c::start()
{
  _sequence = 0;
  _running = true;

  // TODO: implement 24 bit sample format

  /* 4.2.1 Receiver State */
  unsigned char start[8] = { 0x08, 0x00, 0x18, 0x00, 0x80, 0x02, 0x00, 0x00 };
  return transaction( start, sizeof(start) );
}

bool netsdr_source_c::stop()
{
  _running = false;

  /* 4.2.1 Receiver State */
  unsigned char stop[8] = { 0x08, 0x00, 0x18, 0x00, 0x00, 0x01, 0x00, 0x00 };
  return transaction( stop, sizeof(stop) );
}

/* Main work function, pull samples from the socket */
int netsdr_source_c::work(int noutput_items,
                            gr_vector_const_void_star &input_items,
                            gr_vector_void_star &output_items )
{
  udp::endpoint ep;
  unsigned char data[1024*2];

  if ( ! _running )
    return WORK_DONE;

  size_t rx_bytes = _u.receive_from( boost::asio::buffer(data, sizeof(data)), ep );

  #define HEADER_SIZE 2
  #define SEQNUM_SIZE 2

//  bool is_24_bit = false;

  /* check header */
  if ( (0x04 == data[0] && (0x84 == data[1] || 0x82 == data[1])) )
  {
//    is_24_bit = false;
  }
  else if ( (0xA4 == data[0] && 0x85 == data[1]) ||
            (0x84 == data[0] && 0x81 == data[1]) )
  {
//    is_24_bit = true;
    return 0;
  }
  else
    return 0;

  uint16_t sequence = *((uint16_t *)(data + HEADER_SIZE));

  uint16_t diff = sequence - _sequence;

  if ( diff > 1 )
  {
    std::cerr << "Lost " << diff << " packets from NetSDR at " << ep << std::endl;
  }

  _sequence = (0xffff == sequence) ? 0 : sequence;

  /* get pointer to samples */
  int16_t *sample = (int16_t *)(data + HEADER_SIZE + SEQNUM_SIZE);

  size_t rx_samples = (rx_bytes - HEADER_SIZE - SEQNUM_SIZE) / (sizeof(int16_t) * 2);

  #define SCALE_16  (1.0f/32768.0f)

  if ( 1 == _nchan )
  {
    gr_complex *out = (gr_complex *)output_items[0];
    for ( size_t i = 0; i < rx_samples; i++ )
    {
      out[i] = gr_complex( *(sample+0) * SCALE_16,
                           *(sample+1) * SCALE_16 );

      sample += 2;
    }
  }
  else if ( 2 == _nchan )
  {
    rx_samples /= 2;

    gr_complex *out1 = (gr_complex *)output_items[0];
    gr_complex *out2 = (gr_complex *)output_items[1];
    for ( size_t i = 0; i < rx_samples; i++ )
    {
      out1[i] = gr_complex( *(sample+0) * SCALE_16,
                            *(sample+1) * SCALE_16 );

      out2[i] = gr_complex( *(sample+2) * SCALE_16,
                            *(sample+3) * SCALE_16 );

      sample += 4;
    }
  }

  noutput_items = rx_samples;

  return noutput_items;
}

/* discovery protocol internals taken from CuteSDR project */
typedef struct __attribute__ ((__packed__))
{
  /* 56 fixed common byte fields */
  unsigned char length[2]; 	/* length of total message in bytes (little endian byte order) */
  unsigned char key[2];		/* fixed key key[0]==0x5A  key[1]==0xA5 */
  unsigned char op;			/* 0 == Tx_msg(to device), 1 == Rx_msg(from device), 2 == Set(to device) */
  char name[16];				/* Device name string null terminated */
  char sn[16];				/* Serial number string null terminated */
  unsigned char ipaddr[16];	/* device IP address (little endian byte order) */
  unsigned char port[2];		/* device Port number (little endian byte order) */
  unsigned char customfield;	/* Specifies a custom data field for a particular device */
} discover_common_msg_t;

/* UDP port numbers for discovery protocol */
#define DISCOVER_SERVER_PORT 48321	/* PC client Tx port, SDR Server Rx Port */
#define DISCOVER_CLIENT_PORT 48322	/* PC client Rx port, SDR Server Tx Port */

#define KEY0      0x5A
#define KEY1      0xA5
#define MSG_REQ   0
#define MSG_RESP  1
#define MSG_SET   2

typedef struct
{
  std::string name;
  std::string sn;
  std::string addr;
  uint16_t port;
} unit_t;

static void handle_receive( const boost::system::error_code& ec,
                            std::size_t length,
                            boost::system::error_code* out_ec,
                            std::size_t* out_length )
{
  *out_ec = ec;
  *out_length = length;
}

static void handle_timer( const boost::system::error_code& ec,
                          boost::system::error_code* out_ec )
{
  *out_ec = boost::asio::error::timed_out;
}

static std::vector < unit_t > discover_netsdr()
{
  std::vector < unit_t > units;

  boost::system::error_code ec;
  boost::asio::io_service ios;
  udp::socket socket(ios);
  deadline_timer timer(ios);
  unsigned char data[1024*2];

  timer.expires_at(boost::posix_time::pos_infin);

  socket.open(udp::v4(), ec);

  if ( ec )
    return units;

  socket.bind(udp::endpoint(udp::v4(), DISCOVER_CLIENT_PORT), ec);

  if ( ec )
    return units;

  socket.set_option(udp::socket::reuse_address(true));
  socket.set_option(boost::asio::socket_base::broadcast(true));

  discover_common_msg_t tx_msg;
  memset( (void *)&tx_msg, 0, sizeof(discover_common_msg_t) );

  tx_msg.length[0] = sizeof(discover_common_msg_t);
  tx_msg.length[1] = sizeof(discover_common_msg_t) >> 8;
  tx_msg.key[0] = KEY0;
  tx_msg.key[1] = KEY1;
  tx_msg.op = MSG_REQ;

  udp::endpoint ep(boost::asio::ip::address_v4::broadcast(), DISCOVER_SERVER_PORT);
  socket.send_to(boost::asio::buffer(&tx_msg, sizeof(tx_msg)), ep);

  while ( true )
  {
    // Set up the variables that receive the result of the asynchronous
    // operation. The error code is set to would_block to signal that the
    // operation is incomplete. Asio guarantees that its asynchronous
    // operations will never fail with would_block, so any other value in
    // ec indicates completion.
    ec = boost::asio::error::would_block;
    std::size_t rx_bytes = 0;

    // Start the asynchronous receive operation. The handle_receive function
    // used as a callback will update the ec and rx_bytes variables.
    socket.async_receive( boost::asio::buffer(data, sizeof(data)),
        boost::bind(handle_receive, _1, _2, &ec, &rx_bytes) );

    // Set a deadline for the asynchronous operation.
    timer.expires_from_now( boost::posix_time::milliseconds(10) );

    // Start an asynchronous wait on the timer. The handle_timer function
    // used as a callback will update the ec variable.
    timer.async_wait( boost::bind(handle_timer, _1, &ec) );

    // Reset the io_service in preparation for a subsequent run_one() invocation.
    ios.reset();

    // Block until at least one asynchronous operation has completed.
    do ios.run_one(); while ( ec == boost::asio::error::would_block );

    if ( boost::asio::error::timed_out == ec ) /* timer was first to complete */
    {
      // Please note that cancel() has portability issues on some versions of
      // Microsoft Windows, and it may be necessary to use close() instead.
      // Consult the documentation for cancel() for further information.
      socket.cancel();

      break;
    }
    else /* socket was first to complete */
    {
      timer.cancel();
    }

    if ( rx_bytes >= sizeof(discover_common_msg_t) )
    {
      discover_common_msg_t *rx_msg = (discover_common_msg_t *)data;

      if ( KEY0 == rx_msg->key[0] && KEY1 == rx_msg->key[1] &&
           MSG_RESP == rx_msg->op )
      {
        void *temp = rx_msg->port;
        uint16_t port = *((uint16_t *)temp);

        std::string addr = str(boost::format("%d.%d.%d.%d")
            % int(rx_msg->ipaddr[3]) % int(rx_msg->ipaddr[2])
            % int(rx_msg->ipaddr[1]) % int(rx_msg->ipaddr[0]));

        unit_t unit;

        unit.name = rx_msg->name;
        unit.sn = rx_msg->sn;
        unit.addr = addr;
        unit.port = port;

        units.push_back( unit );
      }
    }
  }

  socket.close(ec);

  return units;
}

std::vector<std::string> netsdr_source_c::get_devices( bool fake )
{
  std::vector<std::string> devices;

  std::vector < unit_t > units = discover_netsdr();

  BOOST_FOREACH( unit_t u, units )
  {
//    std::cerr << u.name << " " << u.sn << " " << u.addr <<  ":" << u.port
//              << std::endl;

    devices += str(boost::format("netsdr=%s:%d,label='RFSPACE %s SN %s'")
                   % u.addr % u.port % u.name % u.sn);
  }

  if ( devices.empty() && fake )
    devices += str(boost::format("netsdr=%s:%d,label='RFSPACE NetSDR'")
                   % DEFAULT_HOST % DEFAULT_PORT);

  return devices;
}

size_t netsdr_source_c::get_num_channels()
{
  return _nchan;
}

osmosdr::meta_range_t netsdr_source_c::get_sample_rates()
{
  osmosdr::meta_range_t range;

  #define MAX_RATE 2e6

  /* Calculate NetSDR sample rates */
  for ( size_t i = 625; i >= 10; i-- )
  {
    double rate = 80e6/(4.0*i);

    if ( rate > (MAX_RATE / _nchan) )
      break;

    if ( floor(rate) == rate )
      range += osmosdr::range_t( rate );
  }

  return range;
}

double netsdr_source_c::set_sample_rate( double rate )
{
  unsigned char samprate[9] = { 0x09, 0x00, 0xB8, 0x00, 0x00, 0x20, 0xA1, 0x07, 0x00 };

  uint32_t u32_rate = rate;
  samprate[sizeof(samprate)-4] = u32_rate >>  0;
  samprate[sizeof(samprate)-3] = u32_rate >>  8;
  samprate[sizeof(samprate)-2] = u32_rate >> 16;
  samprate[sizeof(samprate)-1] = u32_rate >> 24;

  std::vector< unsigned char > response;

  // TODO: implement settable sample rates

//  stop();

  if ( _running )
  {
    std::cerr << "Changing the NetSDR sample rate not possible in run mode" << std::endl;
    return get_sample_rate();
  }

  if ( ! transaction( samprate, sizeof(samprate), response ) )
    throw std::runtime_error("set_sample_rate failed");

//  start();

  u32_rate = 0;
  u32_rate |= response[sizeof(samprate)-4] <<  0;
  u32_rate |= response[sizeof(samprate)-3] <<  8;
  u32_rate |= response[sizeof(samprate)-2] << 16;
  u32_rate |= response[sizeof(samprate)-1] << 24;

  _sample_rate = u32_rate;

  if ( rate != _sample_rate )
    std::cerr << "Current NetSDR sample rate is " << (uint32_t)_sample_rate << std::endl;

  return get_sample_rate();
}

double netsdr_source_c::get_sample_rate()
{
  return _sample_rate;
}

osmosdr::freq_range_t netsdr_source_c::get_freq_range( size_t chan )
{
  osmosdr::freq_range_t range;

  /* query freq range(s) of the radio */

  /* 4.2.3 Receiver Frequency */
  unsigned char frange[5] = { 0x05, 0x40, 0x20, 0x00, 0x00 };

  apply_channel( frange, 4, chan );

  std::vector< unsigned char > response;

  transaction( frange, sizeof(frange), response );

  if ( response.size() >= sizeof(frange) + 1 )
  {
    for ( size_t i = 0; i < response[sizeof(frange)]; i++ )
    {
      uint32_t min = *((uint32_t *)&response[sizeof(frange)+1+i*15]);
      uint32_t max = *((uint32_t *)&response[sizeof(frange)+1+5+i*15]);
      //uint32_t vco = *((uint32_t *)&response[sizeof(frange)+1+10+i*15]);

      //std::cerr << min << " " << max << " " << vco << std::endl;

      range += osmosdr::range_t(min, max); /* must be monotonic */
    }
  }

  if ( range.empty() )
    range += osmosdr::range_t(0, 40e6);

  return range;
}

double netsdr_source_c::set_center_freq( double freq, size_t chan )
{
  uint32_t u32_freq = freq;

  /* 4.2.3 Receiver Frequency */
  unsigned char tune[10] = { 0x0A, 0x00, 0x20, 0x00, 0x00, 0xb0, 0x19, 0x6d, 0x00, 0x00 };

  apply_channel( tune, 4, chan );

  tune[sizeof(tune)-5] = u32_freq >>  0;
  tune[sizeof(tune)-4] = u32_freq >>  8;
  tune[sizeof(tune)-3] = u32_freq >> 16;
  tune[sizeof(tune)-2] = u32_freq >> 24;
  tune[sizeof(tune)-1] = 0;

  transaction( tune, sizeof(tune) );

  return get_center_freq( chan );
}

double netsdr_source_c::get_center_freq( size_t chan )
{
  /* 4.2.3 Receiver Frequency */
  unsigned char freq[10] = { 0x05, 0x20, 0x20, 0x00, 0x00 };

  apply_channel( freq, 4, chan );

  std::vector< unsigned char > response;

  if ( ! transaction( freq, sizeof(freq), response ) )
    throw std::runtime_error("get_center_freq failed");

  uint32_t frequency = 0;
  frequency |= response[response.size()-5] <<  0;
  frequency |= response[response.size()-4] <<  8;
  frequency |= response[response.size()-3] << 16;
  frequency |= response[response.size()-2] << 24;

  return frequency;
}

double netsdr_source_c::set_freq_corr( double ppm, size_t chan )
{
  return get_freq_corr( chan );
}

double netsdr_source_c::get_freq_corr( size_t chan )
{
  return 0;
}

std::vector<std::string> netsdr_source_c::get_gain_names( size_t chan )
{
  std::vector< std::string > names;

  names += "ATT";

  return names;
}

osmosdr::gain_range_t netsdr_source_c::get_gain_range( size_t chan )
{
  return osmosdr::gain_range_t(-30, 0, 10);
}

osmosdr::gain_range_t netsdr_source_c::get_gain_range( const std::string & name, size_t chan )
{
  return get_gain_range( chan );
}

bool netsdr_source_c::set_gain_mode( bool automatic, size_t chan )
{
  return false;
}

bool netsdr_source_c::get_gain_mode( size_t chan )
{
  return false;
}

double netsdr_source_c::set_gain( double gain, size_t chan )
{
  /* 4.2.6 RF Gain */
  unsigned char atten[] = { 0x06, 0x00, 0x38, 0x00, 0x00, 0x00 };

  apply_channel( atten, 4, chan );

  if ( gain <= -30 )
    atten[sizeof(atten)-1] = 0xE2;
  else if ( gain <= -20 )
    atten[sizeof(atten)-1] = 0xEC;
  else if ( gain <= -10 )
    atten[sizeof(atten)-1] = 0xF6;
  else /* 0 dB */
    atten[sizeof(atten)-1] = 0x00;

  transaction( atten, sizeof(atten) );

  return get_gain( chan );
}

double netsdr_source_c::set_gain( double gain, const std::string & name, size_t chan )
{
  return set_gain( gain, chan );
}

double netsdr_source_c::get_gain( size_t chan )
{
  /* 4.2.6 RF Gain */
  unsigned char atten[] = { 0x05, 0x20, 0x38, 0x00, 0x00 };

  apply_channel( atten, 4, chan );

  std::vector< unsigned char > response;

  if ( ! transaction( atten, sizeof(atten), response ) )
    throw std::runtime_error("get_gain failed");

  return (char) response[response.size()-1];
}

double netsdr_source_c::get_gain( const std::string & name, size_t chan )
{
  return get_gain( chan );
}

std::vector< std::string > netsdr_source_c::get_antennas( size_t chan )
{
  std::vector< std::string > antennas;

  antennas += get_antenna( chan );

  return antennas;
}

std::string netsdr_source_c::set_antenna( const std::string & antenna, size_t chan )
{
  return get_antenna( chan );
}

std::string netsdr_source_c::get_antenna( size_t chan )
{
  /* We only have a single receive antenna here */
  return "RX";
}

#define BANDWIDTH 34e6

double netsdr_source_c::set_bandwidth( double bandwidth, size_t chan )
{
  /* 4.2.7 RF Filter Selection */
  unsigned char filter[6] = { 0x06, 0x00, 0x44, 0x00, 0x00, 0x00 };

  apply_channel( filter, 4, chan );

  if ( 0.0f == bandwidth )
  {
    _bandwidth = 0.0f;
    filter[sizeof(filter)-1] = 0x00; /* Select bandpass filter based on NCO frequency */
  }
  else if ( BANDWIDTH == bandwidth )
  {
    _bandwidth = BANDWIDTH;
    filter[sizeof(filter)-1] = 0x0B; /* Bypass bandpass filter, use only antialiasing */
  }

  transaction( filter, sizeof(filter) );

  return get_bandwidth();
}

double netsdr_source_c::get_bandwidth( size_t chan )
{
  return _bandwidth;
}

osmosdr::freq_range_t netsdr_source_c::get_bandwidth_range( size_t chan )
{
  osmosdr::freq_range_t bandwidths;

  bandwidths += osmosdr::range_t( BANDWIDTH );

  return bandwidths;
}