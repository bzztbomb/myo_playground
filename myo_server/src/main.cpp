#include <array>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <functional>
#include <chrono>

using namespace std;

#include <boost/program_options.hpp>
namespace po = boost::program_options;

#include "osc/OscOutboundPacketStream.h"
#include "ip/UdpSocket.h"

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

// Myo armband
#include <myo/myo.hpp>

typedef std::array<int8_t, 8> DataPacket;
typedef std::function<void(const DataPacket&)> NewDataCallback;

class DataSource {
public:
  virtual ~DataSource() { };
  virtual void poll() = 0;
  void setCallback(NewDataCallback cb) { mNewDataCallback = cb; }
protected:
  NewDataCallback mNewDataCallback;
};

class RandomDataSource : public DataSource {
public:
  RandomDataSource()
  : mLastTime(RandomClock::now())
  , mFPS(std::chrono::milliseconds((int) (1000.0f / 30.0f)))
  {
  }

  virtual void poll() {
    if (RandomClock::now() - mLastTime < mFPS)
      return;
    DataPacket p;
    for (int i = 0; i < p.size(); i++) {
      p[i] = (((float) rand() / (float) RAND_MAX) * 255.0f) - 127.0f;
    }
    mNewDataCallback(p);
  }
private:
  typedef std::chrono::system_clock RandomClock;
  RandomClock::time_point mLastTime;
  RandomClock::duration mFPS;
};

class DataCollector : public DataSource, public myo::DeviceListener {
public:
  DataCollector()
  : emgSamples()
  {
  }

  ~DataCollector() {
    delete mHub;
  }

  bool connect()
  {
    try {
      std::cout << "Creating hub" << std::endl;
      mHub = new myo::Hub("com.bzztbomb.myo_server");
      std::cout << "Searching for arm band" << std::endl;
      // Next, we attempt to find a Myo to use. If a Myo is already paired in Myo Connect, this will return that Myo
      // immediately.
      // waitForMyo() takes a timeout value in milliseconds. In this case we will try to find a Myo for 10 seconds, and
      // if that fails, the function will return a null pointer.
      mMyo = mHub->waitForMyo(10000);

      // If waitForMyo() returned a null pointer, we failed to find a Myo, so exit with an error message.
      if (!mMyo) {
        throw std::runtime_error("Unable to find a Myo!");
      }

      // We've found a Myo.
      std::cout << "Connected to a Myo armband!" << std::endl << std::endl;

      // Next we enable EMG streaming on the found Myo.
      mMyo->setStreamEmg(myo::Myo::streamEmgEnabled);

      // Hub::addListener() takes the address of any object whose class inherits from DeviceListener, and will cause
      // Hub::run() to send events to all registered device listeners.
      mHub->addListener(this);
    } catch (std::exception e) {
      std::cout << "Failed to connect: " << e.what() << std::endl;
      return false;
    }
    return true;
  }

  void poll() {
    mHub->run(1000/20);
  }

  // onUnpair() is called whenever the Myo is disconnected from Myo Connect by the user.
  void onUnpair(myo::Myo* myo, uint64_t timestamp)
  {
    // We've lost a Myo.
    // Let's clean up some leftover state.
    emgSamples.fill(0);
    if (mNewDataCallback)
      mNewDataCallback(emgSamples);
  }

  // onEmgData() is called whenever a paired Myo has provided new EMG data, and EMG streaming is enabled.
  void onEmgData(myo::Myo* myo, uint64_t timestamp, const int8_t* emg)
  {
    for (int i = 0; i < 8; i++) {
      emgSamples[i] = emg[i];
    }
    if (mNewDataCallback)
      mNewDataCallback(emgSamples);
  }

  // There are other virtual functions in DeviceListener that we could override here, like onAccelerometerData().
  // For this example, the functions overridden above are sufficient.


private:
  myo::Hub* mHub;
  myo::Myo* mMyo;
  // The values of this array is set by onEmgData() above.
  std::array<int8_t, 8> emgSamples;
};

//#define ADDRESS "127.0.0.1"
//#define PORT 7000
//
#define OUTPUT_BUFFER_SIZE 1024

std::vector<UdpTransmitSocket*> oscOutputs;

void addOscOutput(const std::string& in_address, bool broadcast) {
  auto pos = find(in_address.begin(), in_address.end(), ':');
  if (pos == in_address.end()) {
    cerr << in_address << " does specifiy a port, should be address:port" << endl;
    exit(1);
  }
  string address = in_address.substr(0, pos - in_address.begin());
  int port = stoi(in_address.substr(pos - in_address.begin() + 1));
  // Initialize the broadcast sockets
  UdpTransmitSocket* output = new UdpTransmitSocket(IpEndpointName(address.c_str(), port));
  output->SetAllowReuse(true);
  if (broadcast)
    output->SetEnableBroadcast(true);
  oscOutputs.push_back(output);
  cout << "OSC: " << (broadcast ? "Broadcasting on " : "Sending to ") << address << ", port: " << port << endl;
}

typedef websocketpp::server<websocketpp::config::asio> server;
typedef websocketpp::connection_hdl connection_hdl;

typedef std::set<connection_hdl,std::owner_less<connection_hdl>> con_list;

server print_server;
con_list connections;

void on_message(websocketpp::connection_hdl hdl, server::message_ptr msg) {
  std::cout << msg->get_payload() << std::endl;
}

void on_open(connection_hdl hdl) {
  connections.insert(hdl);
}

void on_close(connection_hdl hdl) {
  connections.erase(hdl);
}

void print(const DataPacket& packet) {
  // Clear the current line
  //  std::cout << '\r';
  //
  //  // Print out the EMG data.
  //  for (size_t i = 0; i < packet.size(); i++) {
  //    std::ostringstream oss;
  //    oss << static_cast<int>(packet[i]);
  //    std::string emgString = oss.str();
  //
  //    std::cout << '[' << emgString << std::string(4 - emgString.size(), ' ') << ']';
  //  }
  //
  //  std::cout << std::flush;
  
  // Send OSC
  char buffer[OUTPUT_BUFFER_SIZE];
  osc::OutboundPacketStream p( buffer, OUTPUT_BUFFER_SIZE );
  
  p << osc::BeginBundleImmediate << osc::BeginMessage( "/myo" );
  for (int i = 0; i < packet.size(); i++)
    p << packet[i];
  p << osc::EndMessage << osc::EndBundle;
  
  for (auto i : oscOutputs)
    i->Send(p.Data(), p.Size());
  
  std::stringstream val;
  val << "[ ";
  for (int i = 0; i < packet.size(); i++)
    val << to_string(packet[i]) << (i != packet.size() - 1 ? "," : "");
  val << "]";
  
  // Send WebSocket
  for (auto i : connections) {
    try {
      print_server.send(i, val.str(),websocketpp::frame::opcode::text);
    }
    catch (websocketpp::exception const & e) {
      std::cout << "Exception during send: " << e.what() << std::endl;
    }
  }
}

int main(int argc, char** argv)
{
  po::options_description desc("Allowed options");
  desc.add_options()
    ("help", "produce help message")
    ("oscbroadcast", po::value< vector<string> >(), "broadcast packets on this address:port")
    ("oscsend", po::value< vector<string> >(), "send packets directly to address:port");

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);

  std::vector<std::pair<string, bool>> oscOuts = {
    { "oscbroadcast", true },
    { "oscsend", false }
  };
  for (auto option : oscOuts) {
    if (vm.count(option.first)) {
      for (auto i : vm[option.first].as< vector<string> >())
        addOscOutput(i, option.second);
    }
  }
  
  RandomDataSource random;

  DataCollector collector;
  collector.setCallback(print);

  DataSource* activeSource = &random; //collector.connect() ? &collector : &random;
  activeSource->setCallback(print);

  print_server.set_open_handler(&on_open);
  print_server.set_close_handler(&on_close);
  
  print_server.set_message_handler(&on_message);

  print_server.init_asio();
  print_server.listen(9002);
  print_server.start_accept();
  
  while (1) {
    activeSource->poll();
    print_server.get_io_service().poll();
    this_thread::sleep_for(chrono::milliseconds(1));
  }
}
