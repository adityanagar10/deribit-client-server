"use client";

import React, { useState, useEffect, useCallback } from "react";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from "@/components/ui/table";
import { ScrollArea } from "@/components/ui/scroll-area";

interface Position {
  instrument_name: string;
  size: number;
  average_price: number;
  mark_price: number;
  floating_profit_loss: number;
}

interface PositionsTableProps {
  ws: WebSocket | null;
}

export default function PositionsTable({ ws }: PositionsTableProps) {
  const [positionsData, setPositionsData] = useState<
    Record<string, Record<string, Position[]>>
  >({});
  const [selectedCurrency, setSelectedCurrency] = useState<string>("BTC");
  const [selectedType, setSelectedType] = useState<string>("future");

  const currencies = ["BTC", "ETH", "USDC", "USDT", "EURR", "any"];
  const types = ["future", "option", "spot", "future_combo", "option_combo"];

  const handleMessage = useCallback((event: MessageEvent) => {
    try {
      const data = JSON.parse(event.data);
      if (data.type === "positions_update") {
        setPositionsData((prevData) => ({
          ...prevData,
          [data.currency]: {
            ...prevData[data.currency],
            [data.kind]: data.data,
          },
        }));
      }
    } catch (error) {
      console.error("Error parsing WebSocket message:", error);
    }
  }, []);

  useEffect(() => {
    if (!ws) return;

    ws.addEventListener("message", handleMessage);

    return () => {
      ws.removeEventListener("message", handleMessage);
    };
  }, [ws, handleMessage]);

  useEffect(() => {
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(
        JSON.stringify({
          type: "get_positions",
          currency: selectedCurrency,
          kind: selectedType,
        })
      );
    }
  }, [ws, selectedCurrency, selectedType]);

  const displayedPositions = React.useMemo(() => {
    if (selectedCurrency === "any") {
      return Object.values(positionsData).flatMap(
        (currencyData) => currencyData[selectedType] || []
      );
    } else {
      return positionsData[selectedCurrency]?.[selectedType] || [];
    }
  }, [positionsData, selectedCurrency, selectedType]);

  return (
    <Card>
      <CardHeader>
        <CardTitle>Positions</CardTitle>
      </CardHeader>
      <CardContent>
        <div className='flex space-x-4 mb-4'>
          <div className='w-1/2'>
            <Select
              value={selectedCurrency}
              onValueChange={setSelectedCurrency}
            >
              <SelectTrigger>
                <SelectValue placeholder='Select currency' />
              </SelectTrigger>
              <SelectContent>
                {currencies.map((currency) => (
                  <SelectItem key={currency} value={currency}>
                    {currency}
                  </SelectItem>
                ))}
              </SelectContent>
            </Select>
          </div>
          <div className='w-1/2'>
            <Select value={selectedType} onValueChange={setSelectedType}>
              <SelectTrigger>
                <SelectValue placeholder='Select type' />
              </SelectTrigger>
              <SelectContent>
                {types.map((type) => (
                  <SelectItem key={type} value={type}>
                    {type}
                  </SelectItem>
                ))}
              </SelectContent>
            </Select>
          </div>
        </div>
        <ScrollArea className='h-[400px]'>
          <Table>
            <TableHeader>
              <TableRow>
                <TableHead>Instrument</TableHead>
                <TableHead className='text-right'>Size</TableHead>
                <TableHead className='text-right'>Average Price</TableHead>
                <TableHead className='text-right'>Mark Price</TableHead>
                <TableHead className='text-right'>Floating P/L</TableHead>
              </TableRow>
            </TableHeader>
            <TableBody>
              {displayedPositions.map((position) => (
                <TableRow key={position.instrument_name}>
                  <TableCell className='font-medium'>
                    {position.instrument_name}
                  </TableCell>
                  <TableCell className='text-right'>{position.size}</TableCell>
                  <TableCell className='text-right'>
                    {position.average_price.toFixed(2)}
                  </TableCell>
                  <TableCell className='text-right'>
                    {position.mark_price.toFixed(2)}
                  </TableCell>
                  <TableCell
                    className={`text-right ${
                      position.floating_profit_loss >= 0
                        ? "text-green-600"
                        : "text-red-600"
                    }`}
                  >
                    {position.floating_profit_loss.toFixed(4)}
                  </TableCell>
                </TableRow>
              ))}
            </TableBody>
          </Table>
        </ScrollArea>
      </CardContent>
    </Card>
  );
}
