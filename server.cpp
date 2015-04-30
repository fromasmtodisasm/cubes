// Copyright © 2015, The Network Protocol Company, Inc. All Rights Reserved.

#include "protocol.h"
#include "network.h"
#include "packets.h"
#include "shared.h"
#include "world.h"
#include "game.h"
#include <stdio.h>
#include <signal.h>

enum ClientState
{
    CLIENT_DISCONNECTED,
    CLIENT_CONNECTING,
    CLIENT_CONNECTED
};

struct Server
{
    Socket * socket = nullptr;

    uint64_t client_guid[MaxClients];

    Address client_address[MaxClients];

    ClientState client_state[MaxClients];

    double current_real_time;

    double client_time_last_packet_received[MaxClients];
};

void server_init( Server & server )
{
    server.socket = new Socket( ServerPort );

    for ( int i = 0; i < MaxClients; ++i )
    {
        server.client_guid[i] = 0;
        server.client_state[i] = CLIENT_DISCONNECTED;
        server.client_time_last_packet_received[i] = 0.0;
    }

    server.current_real_time = 0.0;

    printf( "server listening on port %d\n", server.socket->GetPort() );
}

void server_update( Server & server, double current_real_time )
{
    server.current_real_time = current_real_time;

    for ( int i = 0; i < MaxClients; ++i )
    {
        if ( server.client_state[i] != CLIENT_DISCONNECTED )
        {
            if ( current_real_time > server.client_time_last_packet_received[i] + Timeout )
            {
                char buffer[256];
                printf( "client %d timed out - %s\n", i, server.client_address[i].ToString( buffer, sizeof( buffer ) ) );
                server.client_state[i] = CLIENT_DISCONNECTED;
                server.client_address[i] = Address();
                server.client_guid[i] = 0;
                server.client_time_last_packet_received[i] = 0.0;
            }
        }

        switch ( server.client_state[i] )
        {
            /*
            case CLIENT_SENDING_CONNECTION_RESPONSE:
            {
                ConnectionResponsePacket packet;
                packet.type = PACKET_TYPE_CONNECTION_REQUEST;
                packet.client_guid = client.guid;
                client_send_packet( client, packet );
            }
            break;
            */

            default:
                break;
        }
    }
}

int server_find_client_slot( const Server & server, const Address & from, uint64_t client_guid )
{
    for ( int i = 0; i < MaxClients; ++i )
    {
        if ( server.client_guid[i] == client_guid && from == server.client_address[i] )
            return i;
    }
    return -1;
}

int server_find_client_slot( const Server & server, const Address & from )
{
    for ( int i = 0; i < MaxClients; ++i )
    {
        if ( from == server.client_address[i] )
            return i;
    }
    return -1;
}

int server_find_free_slot( const Server & server )
{
    for ( int i = 0; i < MaxClients; ++i )
    {
        if ( server.client_state[i] == CLIENT_DISCONNECTED )
            return i;
    }
    return -1;
}

void server_send_packet( Server & server, const Address & address, Packet & packet )
{
    uint8_t buffer[MaxPacketSize];
    int packet_bytes = 0;
    WriteStream stream( buffer, MaxPacketSize );
    if ( write_packet( stream, packet, packet_bytes ) )
    {
        if ( server.socket->SendPacket( address, buffer, packet_bytes ) && packet.type )
        {
            /*
            char buffer[256];
            printf( "sent %s packet to client %s\n", packet_type_string( packet.type ), address.ToString( buffer, sizeof( buffer ) ) );
            */
        }
    }
}

void server_send_packets( Server & server )
{
    for ( int i = 0; i < MaxClients; ++i )
    {
        if ( server.client_state[i] == CLIENT_CONNECTED )
        {
            SnapshotPacket packet;
            packet.type = PACKET_TYPE_SNAPSHOT;
            packet.tick = 0;                            // todo: we need access to the world tick
            server_send_packet( server, server.client_address[i], packet );
        }
    }
}

bool process_packet( const Address & from, Packet & base_packet, void * context )
{
    Server & server = *(Server*)context;

    /*
    char buffer[256];
    printf( "received %s packet from %s\n", packet_type_string( base_packet.type ), from.ToString( buffer, sizeof( buffer ) ) );
    */

    switch ( base_packet.type )
    {
        case PACKET_TYPE_CONNECTION_REQUEST:
        {
            // is the client already connected?
            ConnectionRequestPacket & packet = (ConnectionRequestPacket&) base_packet;
            int client_slot = server_find_client_slot( server, from, packet.client_guid );
            if ( client_slot == -1 )
            {
                // is there a free client slot?
                client_slot = server_find_free_slot( server );
                if ( client_slot != -1 )
                {
                    char buffer[256];
                    printf( "client %d connecting - %s\n", client_slot, from.ToString( buffer, sizeof( buffer ) ) );

                    // connect client
                    server.client_state[client_slot] = CLIENT_CONNECTING;
                    server.client_guid[client_slot] = packet.client_guid;
                    server.client_address[client_slot] = from;
                    server.client_time_last_packet_received[client_slot] = server.current_real_time;

                    // send connection accepted resonse
                    ConnectionAcceptedPacket response;
                    response.type = PACKET_TYPE_CONNECTION_ACCEPTED;
                    response.client_guid = packet.client_guid;
                    server_send_packet( server, from, response );
                    return true;
                }
                else
                {
                    // no free client slots. send connection denied response
                    ConnectionDeniedPacket response;
                    response.type = PACKET_TYPE_CONNECTION_DENIED;
                    response.client_guid = packet.client_guid;
                    server_send_packet( server, from, response );
                    return true;
                }
            }
            else
            {
                if ( server.client_state[client_slot] == CLIENT_CONNECTING )
                {
                    // we must reply with connection accepted because packets are unreliable
                    ConnectionAcceptedPacket response;
                    response.type = PACKET_TYPE_CONNECTION_ACCEPTED;
                    response.client_guid = packet.client_guid;
                    server_send_packet( server, from, response );
                    return true;
                }
            }
        }
        break;

        case PACKET_TYPE_INPUT:
        {
            //InputPacket & input = (InputPacket&) base_packet;
            int client_slot = server_find_client_slot( server, from );
            if ( client_slot != -1 )
            {
                if ( server.client_state[client_slot] == CLIENT_CONNECTING )
                {
                    char buffer[256];
                    printf( "client %d connected - %s\n", client_slot, from.ToString( buffer, sizeof( buffer ) ) );
                    server.client_state[client_slot] = CLIENT_CONNECTED;
                }

                // todo: process input packet
                server.client_time_last_packet_received[client_slot] = server.current_real_time;
                return true;
            }
        }
        break;

        default:
            break;
    }

    return false;
}

void server_receive_packets( Server & server )
{
    uint8_t buffer[MaxPacketSize];
    while ( true )
    {
        Address from;
        int bytes_read = server.socket->ReceivePacket( from, buffer, sizeof( buffer ) );
        if ( bytes_read == 0 )
            break;
        read_packet( from, buffer, bytes_read, &server );
    }
}

void server_free( Server & server )
{
    printf( "shutting down\n" );
    delete server.socket;
    server = Server();
}

void server_tick( World & world )
{
//    printf( "%d-%d: %f [%+.4f]\n", (int) world.frame, (int) world.tick, world.time, TickDeltaTime );

    world_tick( world );
}

void server_frame( World & world, double real_time, double frame_time, double jitter )
{
    //printf( "%d: %f [%+.2fms]\n", (int) frame, real_time, jitter * 1000 );
    
    for ( int i = 0; i < TicksPerServerFrame; ++i )
    {
        server_tick( world );
    }
}

static volatile int quit = 0;

void interrupt_handler( int dummy )
{
    quit = 1;
}

int server_main( int argc, char ** argv )
{
    printf( "starting server\n" );

    InitializeNetwork();

    Server server;

    server_init( server );

    World world;
    world_init( world );
    world_setup_cubes( world );

    const double start_time = platform_time();

    double previous_frame_time = start_time;
    double next_frame_time = previous_frame_time + ServerFrameDeltaTime;

    signal( SIGINT, interrupt_handler );

    while ( !quit )
    {
        const double time_to_sleep = max( 0.0, next_frame_time - platform_time() - AverageSleepJitter );

        platform_sleep( time_to_sleep );

        const double real_time = platform_time();

        const double frame_time = next_frame_time;

        const double jitter = real_time - frame_time;

        server_update( server, real_time );

        server_receive_packets( server );

        server_send_packets( server );

        server_frame( world, real_time, frame_time, jitter );

        const double end_of_frame_time = platform_time();

        int num_frames_advanced = 0;
        while ( next_frame_time < end_of_frame_time + ServerFrameSafety * ServerFrameDeltaTime )
        {
            next_frame_time += ServerFrameDeltaTime;
            num_frames_advanced++;
        }

        const int num_dropped_frames = max( 0, num_frames_advanced - 1 );

        if ( num_dropped_frames > 0 )
        {
            // todo: would be nice to print out length of previous frame in milliseconds x/y here

            printf( "dropped frame %d (%d)\n", (int) world.frame, num_dropped_frames );
        }

        previous_frame_time = next_frame_time - ServerFrameDeltaTime;

        world.frame++;
    }

    printf( "\n" );

    world_free( world );

    server_free( server );

    ShutdownNetwork();

    return 0;
}

int main( int argc, char ** argv )
{
    return server_main( argc, argv ); 
}
