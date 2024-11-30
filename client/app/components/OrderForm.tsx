"use client";

import React, { useState, useCallback, useEffect } from "react";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { Alert, AlertDescription } from "@/components/ui/alert";
import {
  Card,
  CardContent,
  CardFooter,
  CardHeader,
  CardTitle,
} from "@/components/ui/card";

interface OrderFormProps {
  ws: WebSocket | null;
  instrument: string;
  instrumentType: "future" | "option" | "spot";
}

export default function OrderForm({
  ws,
  instrument,
  instrumentType,
}: OrderFormProps) {
  const [amount, setAmount] = useState("");
  const [price, setPrice] = useState("");
  const [orderType, setOrderType] = useState("market");
  const [direction, setDirection] = useState("buy");
  const [isSubmitting, setIsSubmitting] = useState(false);
  const [message, setMessage] = useState<{
    type: "success" | "error";
    text: string;
  } | null>(null);

  useEffect(() => {
    setAmount("");
    setPrice("");
    setOrderType("market");
    setDirection("buy");
    setMessage(null);
  }, [instrument]);

  const handleSubmit = useCallback(
    async (e: React.FormEvent) => {
      e.preventDefault();
      if (ws && ws.readyState === WebSocket.OPEN) {
        setIsSubmitting(true);
        setMessage(null);
        const order = {
          type: "place_order",
          data: {
            instrument_name: instrument,
            amount: parseFloat(amount),
            type: orderType,
            direction: direction,
            price: orderType === "limit" ? parseFloat(price) : undefined,
          },
        };

        console.log("OrderForm: Sending order:", order);
        ws.send(JSON.stringify(order));
      } else {
        setMessage({ type: "error", text: "WebSocket is not connected" });
      }
    },
    [ws, instrument, amount, price, orderType, direction]
  );

  const handleMessage = useCallback((event: MessageEvent) => {
    const data = JSON.parse(event.data);
    if (data.type === "order_response") {
      setIsSubmitting(false);
      if (data.error) {
        setMessage({
          type: "error",
          text: `Failed to place order: ${data.error}`,
        });
      } else {
        setMessage({
          type: "success",
          text: `Order placed successfully. Order ID: ${
            data.result?.order?.order_id || "N/A"
          }`,
        });
        setAmount("");
        setPrice("");
      }
    }
  }, []);

  useEffect(() => {
    if (ws) {
      ws.addEventListener("message", handleMessage);
      return () => {
        ws.removeEventListener("message", handleMessage);
      };
    }
  }, [ws, handleMessage]);

  return (
    <Card>
      <CardHeader>
        <CardTitle>Place Order</CardTitle>
      </CardHeader>
      <form onSubmit={handleSubmit}>
        <CardContent className='space-y-4'>
          <div className='space-y-2'>
            <Label htmlFor='direction'>Direction</Label>
            <Select value={direction} onValueChange={setDirection}>
              <SelectTrigger id='direction'>
                <SelectValue placeholder='Select direction' />
              </SelectTrigger>
              <SelectContent>
                <SelectItem value='buy'>Buy</SelectItem>
                <SelectItem value='sell'>Sell</SelectItem>
              </SelectContent>
            </Select>
          </div>
          <div className='space-y-2'>
            <Label htmlFor='amount'>
              {instrumentType === "spot" ? "Amount" : "Contracts"}
            </Label>
            <Input
              type='number'
              id='amount'
              value={amount}
              onChange={(e) => setAmount(e.target.value)}
              required
              min='0'
              step={instrumentType === "spot" ? "0.00000001" : "1"}
            />
          </div>
          <div className='space-y-2'>
            <Label htmlFor='orderType'>Order Type</Label>
            <Select value={orderType} onValueChange={setOrderType}>
              <SelectTrigger id='orderType'>
                <SelectValue placeholder='Select order type' />
              </SelectTrigger>
              <SelectContent>
                <SelectItem value='market'>Market</SelectItem>
                <SelectItem value='limit'>Limit</SelectItem>
              </SelectContent>
            </Select>
          </div>
          {orderType === "limit" && (
            <div className='space-y-2'>
              <Label htmlFor='price'>Price</Label>
              <Input
                type='number'
                id='price'
                value={price}
                onChange={(e) => setPrice(e.target.value)}
                required
                min='0'
                step='0.00000001'
              />
            </div>
          )}
          {message && (
            <Alert
              variant={message.type === "success" ? "default" : "destructive"}
            >
              <AlertDescription>{message.text}</AlertDescription>
            </Alert>
          )}
        </CardContent>
        <CardFooter>
          <Button
            type='submit'
            disabled={isSubmitting}
            className={`w-full ${
              direction === "buy"
                ? "bg-green-600 hover:bg-green-700"
                : "bg-red-600 hover:bg-red-700"
            }`}
          >
            {isSubmitting
              ? "Processing..."
              : `Place ${
                  direction.charAt(0).toUpperCase() + direction.slice(1)
                } Order`}
          </Button>
        </CardFooter>
      </form>
    </Card>
  );
}
