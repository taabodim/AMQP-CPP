/**
 *  AMQP.h
 *
 *  Starting point for all includes of the Copernica AMQP library
 *
 *  @documentation public
 */

// base C++ include files
#include <vector>
#include <string>
#include <memory>
#include <map>
#include <queue>
#include <set>
#include <limits>
#include <cstddef>
#include <cstring>
#include <stdexcept>

// base C include files
#include <stdint.h>
#include <math.h>
#include <endian.h>

// forward declarations
#include <amqpcpp/classes.h>

// utility classes
#include <amqpcpp/receivedframe.h>
#include <amqpcpp/outbuffer.h>
#include <amqpcpp/watchable.h>

// amqp types
#include <amqpcpp/field.h>
#include <amqpcpp/numericfield.h>
#include <amqpcpp/decimalfield.h>
#include <amqpcpp/stringfield.h>
#include <amqpcpp/booleanset.h>
#include <amqpcpp/fieldproxy.h>
#include <amqpcpp/table.h>
#include <amqpcpp/array.h>

// envelope for publishing and consuming
#include <amqpcpp/metadata.h>
#include <amqpcpp/envelope.h>
#include <amqpcpp/message.h>

// mid level includes
#include <amqpcpp/exchangetype.h>
#include <amqpcpp/flags.h>
#include <amqpcpp/channelhandler.h>
#include <amqpcpp/channelimpl.h>
#include <amqpcpp/channel.h>
#include <amqpcpp/login.h>
#include <amqpcpp/connectionhandler.h>
#include <amqpcpp/connectionimpl.h>
#include <amqpcpp/connection.h>

