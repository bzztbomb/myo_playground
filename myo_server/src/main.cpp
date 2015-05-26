#include <array>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <functional>

#include <myo/myo.hpp>

#include "osc/OscOutboundPacketStream.h"
#include "ip/UdpSocket.h"

#define ADDRESS "127.0.0.1"
#define PORT 7000

#define OUTPUT_BUFFER_SIZE 1024

UdpTransmitSocket transmitSocket( IpEndpointName( ADDRESS, PORT ) );

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
  virtual void poll() {
    DataPacket p;
    for (int i = 0; i < p.size(); i++) {
      p[i] = (float) rand() / (float) RAND_MAX;
      p[i] *= 255;
      p[i] -= 127;
    }
    mNewDataCallback(p);
  }
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

void print(const DataPacket& packet) {
  // Clear the current line
  std::cout << '\r';

  // Print out the EMG data.
  for (size_t i = 0; i < packet.size(); i++) {
    std::ostringstream oss;
    oss << static_cast<int>(packet[i]);
    std::string emgString = oss.str();

    std::cout << '[' << emgString << std::string(4 - emgString.size(), ' ') << ']';
  }

  std::cout << std::flush;

  // Send OSC
  char buffer[OUTPUT_BUFFER_SIZE];
  osc::OutboundPacketStream p( buffer, OUTPUT_BUFFER_SIZE );

  p << osc::BeginBundleImmediate << osc::BeginMessage( "/myo" );
  for (int i = 0; i < packet.size(); i++)
    p << packet[i];
  p << osc::EndMessage << osc::EndBundle;

  transmitSocket.Send( p.Data(), p.Size() );

  // Send WebSocket
}

int main(int argc, char** argv)
{
  RandomDataSource random;

  DataCollector collector;
  collector.setCallback(print);

  DataSource* activeSource = collector.connect() ? &collector : &random;

  while (1) {
    activeSource->poll();
  }
}
