/**
 *  ConnectionImpl.cpp
 *
 *  Implementation of an AMQP connection
 *
 *  @copyright 2014 Copernica BV
 */
#include "includes.h"
#include "protocolheaderframe.h"
#include "connectioncloseokframe.h"
#include "connectioncloseframe.h"

/**
 *  set namespace
 */
namespace AMQP {

/**
 *  Construct an AMQP object based on full login data
 * 
 *  The first parameter is a handler object. This handler class is
 *  an interface that should be implemented by the caller.
 * 
 *  Note that the constructor is private to ensure that nobody can construct
 *  this class, only the real Connection class via a friend construct
 * 
 *  @param  parent          Parent connection object
 *  @param  handler         Connection handler
 *  @param  login           Login data
 */
ConnectionImpl::ConnectionImpl(Connection *parent, ConnectionHandler *handler, const Login &login, const std::string &vhost) :
    _parent(parent), _handler(handler), _login(login), _vhost(vhost)
{
    // we need to send a protocol header
    send(ProtocolHeaderFrame());
}

/**
 *  Destructor
 */
ConnectionImpl::~ConnectionImpl()
{
    // close the connection in a nice fashion
    close();
    
    // invalidate all channels, so they will no longer call methods on this channel object
    for (auto iter = _channels.begin(); iter != _channels.end(); iter++) iter->second->invalidate();
}

/**
 *  Add a channel to the connection, and return the channel ID that it
 *  is allowed to use, or 0 when no more ID's are available
 *  @param  channel
 *  @return uint16_t
 */
uint16_t ConnectionImpl::add(ChannelImpl *channel)
{
    // check if we have exceeded the limit already
    if (_maxChannels > 0 && _channels.size() >= _maxChannels) return 0;
    
    // keep looping to find an id that is not in use
    while (true)
    {
        // is this id in use?
        if (_nextFreeChannel > 0 && _channels.find(_nextFreeChannel) == _channels.end()) break;
        
        // id is in use, move on
        _nextFreeChannel++;
    }
    
    // we have a new channel
    _channels[_nextFreeChannel] = channel;
    
    // done
    return _nextFreeChannel++;
}

/**
 *  Remove a channel
 *  @param  channel
 */
void ConnectionImpl::remove(ChannelImpl *channel)
{
    // skip zero channel
    if (channel->id() == 0) return;

    // remove it
    _channels.erase(channel->id());
}

/**
 *  Parse the buffer into a recognized frame
 *  
 *  Every time that data comes in on the connection, you should call this method to parse
 *  the incoming data, and let it handle by the AMQP library. This method returns the number
 *  of bytes that were processed.
 *
 *  If not all bytes could be processed because it only contained a partial frame, you should
 *  call this same method later on when more data is available. The AMQP library does not do
 *  any buffering, so it is up to the caller to ensure that the old data is also passed in that
 *  later call.
 *
 *  @param  buffer      buffer to decode
 *  @param  size        size of the buffer to decode
 *  @return             number of bytes that were processed
 */
size_t ConnectionImpl::parse(char *buffer, size_t size)
{
    // do not parse if already in an error state
    if (_state == state_closed) return 0;
    
    // number of bytes processed
    size_t processed = 0;
    
    // create a monitor object that checks if the connection still exists
    Monitor monitor(this);
    
    // keep looping until we have processed all bytes, and the monitor still
    // indicates that the connection is in a valid state
    while (size > 0 && monitor.valid())
    {
        // prevent protocol exceptions
        try
        {
            // try to recognize the frame
            ReceivedFrame receivedFrame(buffer, size, _maxFrame);
            if (!receivedFrame.complete()) return processed;

            // process the frame
            receivedFrame.process(this);

            // number of bytes processed
            size_t bytes = receivedFrame.totalSize();
            
            // add bytes
            processed += bytes; size -= bytes; buffer += bytes;
        }
        catch (const ProtocolException &exception)
        {
            // something terrible happened on the protocol (like data out of range)
            reportError(exception.what());
            
            // done
            return processed;
        }
    }
    
    // done
    return processed;
}

/**
 *  Close the connection
 *  This will close all channels
 *  @return bool
 */
bool ConnectionImpl::close()
{
    // leap out if already closed or closing
    if (_closed) return false;

    // mark that the object is closed
    _closed = true;
    
    // if still busy with handshake, we delay closing for a while
    if (_state == state_handshake || _state == state_protocol) return true;

    // perform the close operation
    sendClose();
    
    // done
    return true;
}

/**
 *  Method to send the close frames
 *  Returns true if object still exists
 *  @return bool
 */
bool ConnectionImpl::sendClose()
{
    // after the send operation the object could be dead
    Monitor monitor(this);
    
    // loop over all channels
    for (auto iter = _channels.begin(); iter != _channels.end(); iter++)
    {
        // close the channel
        iter->second->close();
        
        // we could be dead now
        if (!monitor.valid()) return false;
    }
    
    // send the close frame
    send(ConnectionCloseFrame(0, "shutdown"));
    
    // leap out if object no longer is alive
    if (!monitor.valid()) return false;
    
    // we're in a new state
    _state = state_closing;
    
    // done
    return true;
}

/**
 *  Mark the connection as connected
 */
void ConnectionImpl::setConnected()
{
    // store connected state
    _state = state_connected;

    // if the close operation was already called, we do that again now again
    // so that the actual messages to close down the connection and the channel 
    // are appended to the queue
    if (_closed && !sendClose()) return;
    
    // we're going to call the handler, which can destruct the connection,
    // so we must monitor if the queue object is still valid after calling
    Monitor monitor(this);
    
    // inform handler
    _handler->onConnected(_parent);
    
    // leap out if the connection no longer exists
    if (!monitor.valid()) return;
    
    // empty the queue of messages
    while (!_queue.empty())
    {
        // get the next message
        OutBuffer buffer(std::move(_queue.front()));

        // remove it from the queue
        _queue.pop();
        
        // send it
        _handler->onData(_parent, buffer.data(), buffer.size());
        
        // leap out if the connection was destructed
        if (!monitor.valid()) return;
    }
}

/**
 *  Send a frame over the connection
 *  @param  frame           The frame to send
 *  @return bool            Was the frame succesfully sent
 */
bool ConnectionImpl::send(const Frame &frame)
{
    // its not possible to send anything if closed or closing down
    if (_state == state_closing || _state == state_closed) return false;
    
    // we need an output buffer
    OutBuffer buffer(frame.totalSize());
    
    // fill the buffer
    frame.fill(buffer);
    
    // append an end of frame byte (but not when still negotiating the protocol)
    if (frame.needsSeparator()) buffer.add((uint8_t)206);
    
    // are we still setting up the connection?
    if ((_state == state_connected && _queue.size() == 0) || frame.partOfHandshake())
    {
        // send the buffer
        _handler->onData(_parent, buffer.data(), buffer.size());
    }
    else
    {
        // the connection is still being set up, so we need to delay the message sending
        _queue.push(std::move(buffer));
    }
    
    // done
    return true;
}

/**
 *  End of namspace
 */
}

