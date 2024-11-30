"use client";

import React, { useState, useEffect } from "react";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from "@/components/ui/table";
import { ScrollArea } from "@/components/ui/scroll-area";

interface OrderBookEntry {
  0: number; // price
  1: number; // amount
}

interface OrderBookData {
  asks: OrderBookEntry[];
  bids: OrderBookEntry[];
  instrument_name: string;
}

interface OrderBookProps {
  ws: WebSocket | null;
  instrument: string;
}

const MAX_ROWS = 10; // Maximum number of rows to display for bids and asks

export default function OrderBook({ ws, instrument }: OrderBookProps) {
  const [orderBookData, setOrderBookData] = useState<OrderBookData | null>(
    null
  );
  const [lastUpdate, setLastUpdate] = useState<string>("");

  useEffect(() => {
    if (!ws) return;

    const handleMessage = (event: MessageEvent) => {
      try {
        const data = JSON.parse(event.data);

        if (
          data.type === "orderbook_update" &&
          data.instrument === instrument
        ) {
          console.log("Updating orderbook for", instrument); // Debug log
          setOrderBookData(data.data);
          setLastUpdate(new Date().toLocaleTimeString());
        }
      } catch (error) {
        console.error("Error processing WebSocket message:", error);
      }
    };

    ws.addEventListener("message", handleMessage);

    // Request initial orderbook data
    ws.send(JSON.stringify({ type: "get_orderbook", instrument }));
    console.log("Sent get_orderbook request for", instrument); // Debug log

    return () => {
      ws.removeEventListener("message", handleMessage);
    };
  }, [ws, instrument]);

  const renderOrderBookSide = (
    entries: OrderBookEntry[],
    reverse: boolean = false
  ) => {
    const filledEntries = [...entries];
    while (filledEntries.length < MAX_ROWS) {
      filledEntries.push([0, 0]);
    }

    const rows = filledEntries.slice(0, MAX_ROWS).map((entry, index) => (
      <TableRow key={index} className={entry[0] === 0 ? "invisible" : ""}>
        <TableCell className='text-right'>{entry[0]?.toFixed(2)}</TableCell>
        <TableCell className='text-right'>{entry[1]?.toFixed(4)}</TableCell>
      </TableRow>
    ));

    return reverse ? rows.reverse() : rows;
  };

  return (
    <Card>
      <CardHeader>
        <CardTitle>Order Book - {instrument}</CardTitle>
      </CardHeader>
      <CardContent>
        <p className='text-sm text-muted-foreground mb-4'>
          Last update: {lastUpdate}
        </p>
        {!orderBookData ? (
          <p className='text-center text-muted-foreground'>
            Loading order book data...
          </p>
        ) : (
          <div className='flex'>
            <div className='w-1/2 pr-2'>
              <ScrollArea className='h-[300px]'>
                <Table>
                  <TableHeader>
                    <TableRow>
                      <TableHead className='text-right'>Bid Price</TableHead>
                      <TableHead className='text-right'>Bid Size</TableHead>
                    </TableRow>
                  </TableHeader>
                  <TableBody>
                    {renderOrderBookSide(orderBookData.bids)}
                  </TableBody>
                </Table>
              </ScrollArea>
            </div>
            <div className='w-1/2 pl-2'>
              <ScrollArea className='h-[300px]'>
                <Table>
                  <TableHeader>
                    <TableRow>
                      <TableHead className='text-right'>Ask Price</TableHead>
                      <TableHead className='text-right'>Ask Size</TableHead>
                    </TableRow>
                  </TableHeader>
                  <TableBody>
                    {renderOrderBookSide(orderBookData.asks, true)}
                  </TableBody>
                </Table>
              </ScrollArea>
            </div>
          </div>
        )}
      </CardContent>
    </Card>
  );
}
