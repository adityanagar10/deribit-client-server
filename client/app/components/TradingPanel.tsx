"use client";

import { useRef, useEffect, useState } from "react";
import OrderForm from "./OrderForm";
import OrderBook from "./OrderBook";
import PositionsTable from "./PositionsTable";
import OpenOrders from "./OpenOrders";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { Separator } from "@/components/ui/separator";

interface Instrument {
  instrument_name: string;
  kind: "future" | "option" | "spot";
  expiration_timestamp: number;
  strike?: number;
  option_type?: "call" | "put";
}

export default function TradingPanel() {
  const ws = useRef<WebSocket | null>(null);
  const [isConnected, setIsConnected] = useState(false);
  const [instruments, setInstruments] = useState<Instrument[]>([]);
  const [selectedInstrument, setSelectedInstrument] = useState<string>("");
  const [currency, setCurrency] = useState<"BTC" | "ETH">("BTC");
  const [instrumentType, setInstrumentType] = useState<
    "future" | "option" | "spot"
  >("future");

  useEffect(() => {
    ws.current = new WebSocket("ws://localhost:9002");

    ws.current.onopen = () => {
      console.log("WebSocket connection established");
      setIsConnected(true);
      fetchInstruments();
    };

    ws.current.onmessage = (event) => {
      const data = JSON.parse(event.data);
      if (data.type === "instruments") {
        setInstruments(data.result);
        if (data.result.length > 0) {
          setSelectedInstrument(data.result[0].instrument_name);
        }
      }
    };

    ws.current.onclose = () => {
      console.log("WebSocket connection closed");
      setIsConnected(false);
    };

    ws.current.onerror = (error) => {
      console.error("WebSocket error:", error);
      setIsConnected(false);
    };

    return () => {
      if (ws.current) {
        ws.current.close();
      }
    };
  }, []);

  useEffect(() => {
    fetchInstruments();
  }, [currency, instrumentType]);

  const fetchInstruments = () => {
    if (ws.current && ws.current.readyState === WebSocket.OPEN) {
      ws.current.send(
        JSON.stringify({
          type: "get_instruments",
          currency,
          kind: instrumentType,
        })
      );
    }
  };

  if (!isConnected) {
    return <div className='text-center mt-8'>Connecting to server...</div>;
  }

  return (
    <div className='container mx-auto p-6 space-y-8'>
      <Card>
        <CardHeader>
          <CardTitle>Select Instrument</CardTitle>
        </CardHeader>
        <CardContent>
          <div className='grid grid-cols-1 md:grid-cols-3 gap-4'>
            <div>
              <Select
                value={currency}
                onValueChange={(value) => setCurrency(value as "BTC" | "ETH")}
              >
                <SelectTrigger>
                  <SelectValue placeholder='Select currency' />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value='BTC'>BTC</SelectItem>
                  <SelectItem value='ETH'>ETH</SelectItem>
                </SelectContent>
              </Select>
            </div>
            <div>
              <Select
                value={instrumentType}
                onValueChange={(value) =>
                  setInstrumentType(value as "future" | "option" | "spot")
                }
              >
                <SelectTrigger>
                  <SelectValue placeholder='Select instrument type' />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value='future'>Future</SelectItem>
                  <SelectItem value='option'>Option</SelectItem>
                  <SelectItem value='spot'>Spot</SelectItem>
                </SelectContent>
              </Select>
            </div>
            <div>
              <Select
                value={selectedInstrument}
                onValueChange={setSelectedInstrument}
              >
                <SelectTrigger>
                  <SelectValue placeholder='Select instrument' />
                </SelectTrigger>
                <SelectContent>
                  {instruments.map((instrument) => (
                    <SelectItem
                      key={instrument.instrument_name}
                      value={instrument.instrument_name}
                    >
                      {instrument.instrument_name}
                      {instrument.kind === "option" &&
                        ` (${instrument.option_type?.toUpperCase()} ${
                          instrument.strike
                        })`}
                      {instrument.kind !== "spot" &&
                        ` (Expires: ${new Date(
                          instrument.expiration_timestamp
                        ).toLocaleDateString()})`}
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
            </div>
          </div>
        </CardContent>
      </Card>

      <div className='grid grid-cols-1 lg:grid-cols-2 gap-8'>
        <div className='space-y-8'>
          <Card>
            <CardHeader>
              <CardTitle>Order Book - {selectedInstrument}</CardTitle>
            </CardHeader>
            <CardContent>
              <OrderBook ws={ws.current} instrument={selectedInstrument} />
            </CardContent>
          </Card>
          <Card>
            <CardHeader>
              <CardTitle>Place Order</CardTitle>
            </CardHeader>
            <CardContent>
              <OrderForm
                ws={ws.current}
                instrument={selectedInstrument}
                instrumentType={instrumentType}
              />
            </CardContent>
          </Card>
        </div>
        <div className='space-y-8'>
          <Card>
            <CardHeader>
              <CardTitle>Open Orders</CardTitle>
            </CardHeader>
            <CardContent>
              <OpenOrders ws={ws.current} />
            </CardContent>
          </Card>
        </div>
      </div>

      <Card>
        <CardHeader>
          <CardTitle>Your Positions</CardTitle>
        </CardHeader>
        <CardContent>
          <PositionsTable ws={ws.current} />
        </CardContent>
      </Card>
    </div>
  );
}
