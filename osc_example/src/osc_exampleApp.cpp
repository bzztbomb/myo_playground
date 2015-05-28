#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/gl/gl.h"
#include "cinder/gl/Shader.h"

#include "OscListener.h"

using namespace ci;
using namespace ci::app;
using namespace std;

void drawBuffer(const std::deque<int8_t>& buffer, const Rectf &bounds, bool drawFrame, const ci::ColorA &color, float scaleFactor )
{
  gl::ScopedGlslProg glslScope( getStockShader( gl::ShaderDef().color() ) );
  
  gl::color( color );
  
  const float waveHeight = bounds.getHeight();
  const float xScale = bounds.getWidth() / (float)buffer.size();
  
  float yOffset = bounds.y1;
  PolyLine2f waveform;
  float x = bounds.x1;
  for( size_t i = 0; i < buffer.size(); i++ ) {
    x += xScale;
    float y = ( 1.0f - ( buffer[i] * scaleFactor + 0.5f) ) * waveHeight + yOffset;
    waveform.push_back( vec2( x, y ) );
  }
  
  if( ! waveform.getPoints().empty() )
    gl::draw( waveform );
  
  if( drawFrame ) {
    gl::color( color.r, color.g, color.b, color.a * 0.6f );
    gl::drawStrokedRect( bounds );
  }
}

class osc_exampleApp : public App {
public:
	void setup() override;
	void mouseDown( MouseEvent event ) override;
	void update() override;
	void draw() override;
private:
  osc::Listener mListener;
  std::array<std::deque<int8_t>, 8> mEMG;
};

void osc_exampleApp::setup()
{
  mListener.setup(23232);
  for (auto& emg : mEMG) {
    emg.resize(1000);
    std::fill(emg.begin(), emg.end(), 0);
  }
}

void osc_exampleApp::mouseDown( MouseEvent event )
{
}

void osc_exampleApp::update()
{
  while(mListener.hasWaitingMessages() ) {
    osc::Message message;
    mListener.getNextMessage( &message );
    
    if (message.getAddress() == "/myo") {
      for (int i = 0; i < min((size_t)message.getNumArgs(), mEMG.size()); i++) {
        if( message.getArgType(i) == osc::TYPE_INT32 ) {
          mEMG[i].push_front(message.getArgAsInt32(i));
          mEMG[i].pop_back();
        }
      }
    } else {
      console() << "Unknown message: " << message.getAddress() << endl;
    }
  }
}

void osc_exampleApp::draw()
{
  gl::clear( Color( 0, 0, 0 ) );
  
  float width = getWindowWidth();
  float height = getWindowHeight() / mEMG.size();
  float currY = 0.0f;
  vec3 currColor = vec3(0.0f, 1.0f, 1.0f);
  float hInc = 1.0 / mEMG.size();
  for (auto emg : mEMG) {
    ColorA c = hsvToRgb(currColor);
    drawBuffer(emg, Rectf(0, currY, width, currY + height), true, c, 1.0f / 256.0f);
    currColor.r += hInc;
    currY += height;
  }
}

CINDER_APP( osc_exampleApp, RendererGl )
