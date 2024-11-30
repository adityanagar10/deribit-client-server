"use client";

import React, { useState, useEffect } from "react";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Alert, AlertDescription } from "@/components/ui/alert";
import {
  Card,
  CardContent,
  CardFooter,
  CardHeader,
  CardTitle,
} from "@/components/ui/card";

interface ModifyOrderFormProps {
  ws: WebSocket | null;
  orderId: string;
  currentAmount: number;
  currentPrice: number | null;
}

export default function ModifyOrderForm({
  ws,
  orderId,
  currentAmount,
  currentPrice,
}: ModifyOrderFormProps) {
  const [amount, setAmount] = useState(currentAmount.toString());
  const [price, setPrice] = useState(
    currentPrice ? currentPrice.toString() : ""
  );
  const [isSubmitting, setIsSubmitting] = useState(false);
  const [message, setMessage] = useState<{
    type: "success" | "error";
    text: string;
  } | null>(null);

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    setIsSubmitting(true);
    setMessage(null);

    const modifyData = {
      order_id: orderId,
      amount: parseFloat(amount),
      price: price ? parseFloat(price) : undefined,
    };

    ws?.send(JSON.stringify({ type: "modify_order", data: modifyData }));
  };

  useEffect(() => {
    const handleMessage = (event: MessageEvent) => {
      const data = JSON.parse(event.data);
      if (data.type === "modify_response") {
        setIsSubmitting(false);
        if (data.error) {
          setMessage({
            type: "error",
            text: `Failed to modify order: ${data.error}`,
          });
        } else {
          setMessage({ type: "success", text: "Order modified successfully" });
        }
      }
    };

    ws?.addEventListener("message", handleMessage);

    return () => {
      ws?.removeEventListener("message", handleMessage);
    };
  }, [ws]);

  return (
    <Card>
      <CardHeader>
        <CardTitle>Modify Order</CardTitle>
      </CardHeader>
      <form onSubmit={handleSubmit}>
        <CardContent className='space-y-4'>
          <div className='space-y-2'>
            <Label htmlFor='amount'>New Amount</Label>
            <Input
              type='number'
              id='amount'
              value={amount}
              onChange={(e) => setAmount(e.target.value)}
              required
            />
          </div>
          {currentPrice !== null && (
            <div className='space-y-2'>
              <Label htmlFor='price'>New Price (optional)</Label>
              <Input
                type='number'
                id='price'
                value={price}
                onChange={(e) => setPrice(e.target.value)}
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
          <Button type='submit' disabled={isSubmitting} className='w-full'>
            {isSubmitting ? "Modifying..." : "Modify Order"}
          </Button>
        </CardFooter>
      </form>
    </Card>
  );
}
